/* Exercise real dispatch/persistence/JSON with fake hardware and NVS.
 * Including dispatch gives this test a reboot reset without a production hook. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../main/sensor_config.c"
#include "nvs.h"

static unsigned char *stored;
static size_t stored_size;
static unsigned char *pending;
static size_t pending_size;
static bool fail_commit;
static int writes;

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    *handle = 1;
    return mode == NVS_READONLY && stored == NULL ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size)
{
    if (stored == NULL) return ESP_ERR_NVS_NOT_FOUND;
    if (data != NULL)
    {
        if (*size < stored_size) return ESP_ERR_INVALID_SIZE;
        memcpy(data, stored, stored_size);
    }
    *size = stored_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    free(pending);
    pending = malloc(size);
    assert(pending);
    memcpy(pending, data, size);
    pending_size = size;
    ++writes;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    if (fail_commit) return ESP_FAIL;
    free(stored);
    stored = pending;
    stored_size = pending_size;
    pending = NULL;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { }

/* A second model shares provider A's singleton. Both may claim "ambiguous". */
static int values[2], validations[2], restores[2], starts[2], control_actions[2];
static esp_err_t control_results[2];
static bool fail_start;
#define MOCK_PROVIDER(prefix, index, model) \
    const void *prefix##_config_get(void) { return &values[index]; } \
    size_t prefix##_config_size(void) { return sizeof(int); } \
    size_t prefix##_supported_type_count(void) { return 1; } \
    const char *prefix##_supported_type(size_t type_index) { \
        return type_index == 0 ? model : NULL; } \
    bool prefix##_can_serve_type(const char *type) { \
        return type && (!strcmp(type, model) || !strcmp(type, "ambiguous") || \
            (index == 0 && !strcmp(type, "model-a2"))); } \
    esp_err_t prefix##_configure(const cJSON *json) { \
        ++validations[index]; \
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, "value"); \
        values[index] = -999; /* Deliberately mutate even on validation failure. */ \
        if (!cJSON_IsNumber(v) || v->valueint < 0) return ESP_ERR_INVALID_ARG; \
        values[index] = v->valueint; return ESP_OK; } \
    esp_err_t prefix##_config_restore(const void *record) { \
        if (record == NULL) return ESP_ERR_INVALID_ARG; \
        ++restores[index]; memcpy(&values[index], record, sizeof(int)); return ESP_OK; } \
    esp_err_t prefix##_start(void) { ++starts[index]; return fail_start ? ESP_FAIL : ESP_OK; } \
    esp_err_t prefix##_control_action(const char *payload, int payload_length) { \
        (void)payload; (void)payload_length; ++control_actions[index]; return control_results[index]; } \
    esp_err_t prefix##_config_to_json(json_gen_str_t *json) { \
        return json_gen_obj_set_int(json, "value", values[index]) == 0 ? ESP_OK : ESP_FAIL; }
MOCK_PROVIDER(angle_sensor, 0, "model-a")
MOCK_PROVIDER(uptime_sensor, 1, "model-b")

static esp_err_t configure(const char *name, const char *type, int value)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", name);
    cJSON_AddStringToObject(json, "type", type);
    cJSON_AddNumberToObject(json, "value", value);
    esp_err_t err = sensor_config_from_mqtt(json);
    cJSON_Delete(json); /* Lifetime regression: registry must own every field. */
    return err;
}

static void reboot(void)
{
    registry_clear(registry_init());
    active_lookup = NULL;
    memset(values, 0, sizeof(values));
    memset(validations, 0, sizeof(validations));
    memset(restores, 0, sizeof(restores));
    memset(starts, 0, sizeof(starts));
}

int main(void)
{
    assert(registry_init_on_boot() == ESP_OK);
    assert(active_lookup->count == 0 && starts[0] == 1 && starts[1] == 1);
    assert(sensor_provider_count() == 2);
    assert(!strcmp(sensor_provider_name(0), "angle_sensor"));
    assert(sensor_provider_number("angle_sensor") == 1);
    assert(sensor_provider_number("uptime_sensor") == 2);
    assert(sensor_provider_number("missing") == 0);
    assert(sensor_provider_handle_control("uptime_sensor", "{}", 2) == ESP_OK);
    assert(control_actions[1] == 1);
    control_results[1] = ESP_FAIL;
    assert(sensor_provider_handle_control("uptime_sensor", "{}", 2) == ESP_FAIL);
    control_results[1] = ESP_OK;
    assert(sensor_provider_handle_control("missing", "{}", 2) == ESP_ERR_INVALID_ARG);
    char json[1024];
    json_gen_str_t catalogue_generator;
    json_gen_str_start(&catalogue_generator, json, sizeof(json), NULL, NULL);
    assert(json_gen_start_array(&catalogue_generator) == 0);
    assert(sensor_available_providers_json_add(&catalogue_generator));
    assert(json_gen_end_array(&catalogue_generator) == 0);
    assert(json_gen_str_end(&catalogue_generator) > 1);
    assert(!strcmp(json, "[{\"name\":\"angle_sensor\",\"types\":[\"model-a\"]},"
                       "{\"name\":\"uptime_sensor\",\"types\":[\"model-b\"]}]"));
    size_t catalogue_length =
        sensor_available_providers_json_dump(json, sizeof(json));
    assert(catalogue_length == strlen(json));
    assert(!strcmp(json, "{\"providers\":[{\"name\":\"angle_sensor\",\"types\":[\"model-a\"]},"
                       "{\"name\":\"uptime_sensor\",\"types\":[\"model-b\"]}]}"));
    assert(registry_providers_json_dump(json, sizeof(json)) == 2);
    assert(strcmp(json, "[]") == 0);
    assert(configure("unknown", "missing", 1) == ESP_ERR_INVALID_ARG);
    assert(configure("unknown", "ambiguous", 1) == ESP_ERR_INVALID_ARG);
    assert(sensor_config_from_mqtt(NULL) == ESP_ERR_INVALID_ARG);
    assert(writes == 0 && validations[0] == 0 && validations[1] == 0);

    assert(configure("rudder\"\\\n", "model-a", 7) == ESP_OK);
    assert(configure("clock", "model-b", 60) == ESP_OK);
    assert(active_lookup->count == 2);
    assert(configure("rudder\"\\\n", "model-a", 9) == ESP_OK);
    assert(active_lookup->count == 2);
    int before = writes;
    assert(configure("other", "model-a2", 1) == ESP_ERR_INVALID_STATE);
    assert(configure("clock", "model-a", 1) == ESP_ERR_INVALID_STATE);
    assert(configure("clock", "model-b", -1) == ESP_ERR_INVALID_ARG);
    assert(values[1] == 60 && writes == before);
    fail_start = true;
    assert(configure("clock", "model-b", 17) == ESP_FAIL);
    assert(values[1] == 60 && writes == before);
    fail_start = false;

    size_t provider_length =
        registry_provider_json_dump("uptime_sensor", json, sizeof(json));
    assert(provider_length == strlen(json));
    cJSON *provider_dump = cJSON_Parse(json);
    assert(cJSON_GetObjectItem(provider_dump, "value")->valueint == 60);
    cJSON_Delete(provider_dump);
    assert(registry_provider_json_dump("missing", json, sizeof(json)) == 0);
    const char *provider_name;
    const char *provider_type;
    assert(registry_provider_identity("uptime_sensor", &provider_name,
                                      &provider_type));
    assert(!strcmp(provider_name, "clock"));
    assert(!strcmp(provider_type, "model-b"));
    assert(!registry_provider_identity("missing", &provider_name,
                                       &provider_type));

    json_gen_str_t active_generator;
    json_gen_str_start(&active_generator, json, sizeof(json), NULL, NULL);
    assert(json_gen_start_array(&active_generator) == 0);
    assert(sensor_active_providers_json_add(&active_generator));
    assert(json_gen_end_array(&active_generator) == 0);
    assert(json_gen_str_end(&active_generator) > 1);
    assert(!strcmp(json, "[1,2]"));

    size_t length = registry_providers_json_dump(json, sizeof(json));
    assert(length == strlen(json));
    cJSON *dump = cJSON_Parse(json);
    assert(cJSON_GetArraySize(dump) == 2);
    assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(dump, 0), "name")->valuestring,
                   "rudder\"\\\n"));
    assert(cJSON_GetObjectItem(cJSON_GetArrayItem(dump, 1), "value")->valueint == 60);
    cJSON_Delete(dump);
    assert(registry_providers_json_dump(json, length + 1) == length);
    assert(registry_providers_json_dump(json, length) == 0);

    json_gen_str_t generator;
    json_gen_str_start(&generator, json, sizeof(json), NULL, NULL);
    assert(json_gen_start_object(&generator) == 0);
    assert(json_gen_push_array(&generator, "features") == 0);
    assert(registry_providers_json_add(&generator));
    assert(json_gen_pop_array(&generator) == 0);
    assert(json_gen_end_object(&generator) == 0);
    assert(json_gen_str_end(&generator) > 1);
    dump = cJSON_Parse(json);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(dump, "features")) == 2);
    cJSON_Delete(dump);

    reboot();
    assert(registry_init_on_boot() == ESP_OK);
    assert(active_lookup->count == 2 && values[0] == 9 && values[1] == 60);
    assert(validations[0] == 0 && validations[1] == 0);
    assert(starts[0] == 1 && starts[1] == 1);
    assert(registry_init_on_boot() == ESP_OK && starts[0] == 1);

    /* Every truncation must fail atomically, preserving the existing entries. */
    size_t full_size = stored_size;
    feature_entry_t *first = active_lookup->configurations[0];
    for (size_t n = 0; n < full_size; ++n)
    {
        stored_size = n;
        assert(registry_read(active_lookup) != ESP_OK);
        assert(active_lookup->count == 2 && active_lookup->configurations[0] == first);
    }
    stored_size = full_size;

    fail_commit = true;
    assert(configure("clock", "model-b", 30) == ESP_FAIL);
    assert(values[1] == 30 && active_lookup->count == 2);
    fail_commit = false;
    assert(configure("clock", "model-b", 30) == ESP_OK);
    reboot();
    assert(registry_init_on_boot() == ESP_OK && values[1] == 30);

    /* Unsupported persisted type fails before any sensor starts or validates. */
    feature_entry_t *entry = active_lookup->configurations[0];
    free(entry->type);
    entry->type = strdup("removed-provider");
    assert(registry_write(active_lookup) == ESP_OK);
    reboot();
    assert(registry_init_on_boot() == ESP_ERR_INVALID_STATE);
    assert(active_lookup->count == 0 && starts[0] == 0 && starts[1] == 0);
    assert(validations[0] == 0 && validations[1] == 0);

    /* Multiple names cannot restore into the same singleton provider. */
    int snapshot = 42;
    active_lookup->configurations[0] = registry_entry_create("first", "model-a", &snapshot, sizeof(snapshot));
    active_lookup->configurations[1] = registry_entry_create("second", "model-a2", &snapshot, sizeof(snapshot));
    active_lookup->count = 2;
    assert(registry_write(active_lookup) == ESP_OK);
    reboot();
    assert(registry_init_on_boot() == ESP_ERR_INVALID_STATE);
    assert(active_lookup->count == 0 && starts[0] == 0);

    /* An incompatible binary record is rejected before copying to the provider. */
    char bad_snapshot = 1;
    active_lookup->configurations[0] = registry_entry_create("first", "model-a", &bad_snapshot, 1);
    active_lookup->count = 1;
    assert(registry_write(active_lookup) == ESP_OK);
    reboot();
    assert(registry_init_on_boot() == ESP_ERR_INVALID_STATE);
    assert(active_lookup->count == 0 && restores[0] == 0);

    assert(registry_write(active_lookup) == ESP_OK);
    reboot();
    assert(registry_init_on_boot() == ESP_OK && active_lookup->count == 0);
    registry_clear(registry_init());
    free(stored);
    free(pending);
    puts("Registry lifecycle tests passed");
}
