/* Exercise instance dispatch, persistence and JSON with fake hardware/NVS. */
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
static bool fail_start;
static int writes;
static int validations[2], restores[2], starts[2], destroys[2];
static int control_actions[2];
static esp_err_t control_results[2];

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
  *handle = 1;
  return mode == NVS_READONLY && stored == NULL ? ESP_ERR_NVS_NOT_FOUND
                                                : ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data,
                       size_t *size) {
  if (stored == NULL) return ESP_ERR_NVS_NOT_FOUND;
  if (data != NULL) {
    if (*size < stored_size) return ESP_ERR_INVALID_SIZE;
    memcpy(data, stored, stored_size);
  }
  *size = stored_size;
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data,
                       size_t size) {
  free(pending);
  pending = malloc(size);
  assert(pending != NULL);
  memcpy(pending, data, size);
  pending_size = size;
  ++writes;
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
  if (fail_commit) return ESP_FAIL;
  free(stored);
  stored = pending;
  stored_size = pending_size;
  pending = NULL;
  return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {}

typedef struct {
  int provider;
  int value;
} mock_instance_t;

#define MOCK_PROVIDER(prefix, index, model)                                    \
  bool prefix##_can_serve_type(const char *type) {                            \
    return type != NULL &&                                                     \
           (!strcmp(type, model) || !strcmp(type, "ambiguous") ||             \
            ((index) == 0 && !strcmp(type, "model-a2")));                     \
  }                                                                            \
  size_t prefix##_supported_type_count(void) { return (index) == 0 ? 2U : 1U; } \
  const char *prefix##_supported_type(size_t type_index) {                    \
    if (type_index == 0U) return model;                                        \
    return (index) == 0 && type_index == 1U ? "model-a2" : NULL;              \
  }                                                                            \
  void *prefix##_create(const char *type) {                                   \
    if (!prefix##_can_serve_type(type) || !strcmp(type, "ambiguous"))         \
      return NULL;                                                             \
    mock_instance_t *instance = calloc(1, sizeof(*instance));                 \
    if (instance != NULL) instance->provider = index;                         \
    return instance;                                                          \
  }                                                                            \
  void prefix##_destroy(void *opaque) {                                       \
    if (opaque != NULL) ++destroys[index];                                    \
    free(opaque);                                                              \
  }                                                                            \
  const void *prefix##_config_get(const void *opaque) {                       \
    return &((const mock_instance_t *)opaque)->value;                         \
  }                                                                            \
  size_t prefix##_config_size(void) { return sizeof(int); }                   \
  esp_err_t prefix##_configure(void *opaque, const cJSON *json) {             \
    mock_instance_t *instance = opaque;                                       \
    ++validations[index];                                                      \
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "value");     \
    if (value == NULL) return ESP_OK;                                          \
    instance->value = -999;                                                    \
    if (!cJSON_IsNumber(value) || value->valueint < 0)                        \
      return ESP_ERR_INVALID_ARG;                                              \
    instance->value = value->valueint;                                        \
    return ESP_OK;                                                             \
  }                                                                            \
  esp_err_t prefix##_config_restore(void *opaque, const void *record) {       \
    if (opaque == NULL || record == NULL) return ESP_ERR_INVALID_ARG;          \
    ++restores[index];                                                         \
    memcpy(&((mock_instance_t *)opaque)->value, record, sizeof(int));          \
    return ESP_OK;                                                             \
  }                                                                            \
  esp_err_t prefix##_start(void *opaque) {                                    \
    ++starts[index];                                                           \
    return fail_start ? ESP_FAIL : ESP_OK;                                    \
  }                                                                            \
  esp_err_t prefix##_working_topics_json_add(const void *opaque,              \
                                              json_gen_str_t *json) {          \
    return json_gen_arr_set_string(json, "working/" model) == 0 ? ESP_OK      \
                                                                 : ESP_FAIL;  \
  }                                                                            \
  esp_err_t prefix##_control_action(void *opaque, const char *instance_name,  \
                                    const char *payload, int payload_length) { \
    ++control_actions[index];                                                  \
    return control_results[index];                                             \
  }                                                                            \
  esp_err_t prefix##_config_to_json(const void *opaque, json_gen_str_t *json) { \
    return json_gen_obj_set_int(json, "value",                                \
                                ((const mock_instance_t *)opaque)->value) == 0 \
               ? ESP_OK                                                       \
               : ESP_FAIL;                                                    \
  }

MOCK_PROVIDER(angle_sensor, 0, "model-a")
MOCK_PROVIDER(uptime_sensor, 1, "model-b")

static esp_err_t configure(const char *target, const char *name,
                           const char *type, int value) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "name", name);
  if (type != NULL) cJSON_AddStringToObject(json, "type", type);
  cJSON_AddNumberToObject(json, "value", value);
  const esp_err_t err = sensor_provider_configure_from_mqtt(target, json);
  cJSON_Delete(json);
  return err;
}

static esp_err_t add(const char *name, const char *type, int value) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "action", "add_provider");
  if (name != NULL) cJSON_AddStringToObject(json, "name", name);
  if (type != NULL) cJSON_AddStringToObject(json, "type", type);
  cJSON_AddNumberToObject(json, "value", value);
  const esp_err_t err = sensor_provider_add_from_mqtt(json);
  cJSON_Delete(json);
  return err;
}

static int instance_value(const char *name) {
  const size_t index = instance_index_for_name(name);
  assert(index != SIZE_MAX);
  return ((mock_instance_t *)runtimes[index].instance)->value;
}

static void reboot(void) {
  runtime_clear();
  registry_clear(registry_init());
  active_lookup = NULL;
  memset(validations, 0, sizeof(validations));
  memset(restores, 0, sizeof(restores));
  memset(starts, 0, sizeof(starts));
}

int main(void) {
  /* First boot materializes and persists all provider defaults. */
  assert(registry_init_on_boot() == ESP_OK);
  assert(sensor_provider_count() == 2U && writes == 1);
  assert(!strcmp(sensor_provider_name(0), "1"));
  assert(!strcmp(sensor_provider_name(1), "2"));
  assert(sensor_provider_number("1") == 1U);
  assert(sensor_provider_number("2") == 2U);
  assert(starts[0] == 1 && starts[1] == 1);

  char component[SENSOR_CONFIG_NAME_SIZE];
  assert(sensor_provider_control_component("1", component, sizeof(component)));
  assert(!strcmp(component, "1"));
  assert(sensor_provider_handle_control("2", "{}", 2) == ESP_OK);
  assert(control_actions[1] == 1);
  assert(sensor_provider_handle_control("missing", "{}", 2) ==
         ESP_ERR_INVALID_ARG);

  char json[2048];
  const size_t catalogue_length =
      sensor_available_providers_json_dump(json, sizeof(json));
  assert(catalogue_length == strlen(json));
  assert(!strcmp(json,
                 "{\"providers\":[{\"name\":\"angle_sensor\",\"types\":["
                 "\"model-a\",\"model-a2\"]},{\"name\":\"uptime_sensor\","
                 "\"types\":[\"model-b\"]}]}"));

  /* Configure remains scoped to the addressed instance and may rename it. */
  assert(configure("1", "rudder", "model-a", 7) == ESP_OK);
  assert(configure("2", "clock", NULL, 60) == ESP_OK);
  assert(instance_value("rudder") == 7 && instance_value("clock") == 60);
  assert(configure("rudder", "clock", "model-a", 8) ==
         ESP_ERR_INVALID_STATE);
  assert(configure("rudder", "bad/name", "model-a", 8) ==
         ESP_ERR_INVALID_ARG);
  assert(configure("rudder", "rudder", "model-a2", 8) ==
         ESP_ERR_INVALID_ARG);

  /* add_provider accepts settings and creates independent same-type state. */
  assert(add("port", "model-a", 11) == ESP_OK);
  assert(add("starboard", "model-a2", 22) == ESP_OK);
  assert(sensor_provider_count() == 4U);
  assert(instance_value("rudder") == 7);
  assert(instance_value("port") == 11);
  assert(instance_value("starboard") == 22);
  assert(add("port", "model-b", 1) == ESP_ERR_INVALID_STATE);
  assert(add("bad/name", "model-b", 1) == ESP_ERR_INVALID_ARG);
  assert(add("unknown", "missing", 1) == ESP_ERR_INVALID_ARG);
  assert(add("ambiguous", "ambiguous", 1) == ESP_ERR_INVALID_ARG);

  /* Reconfiguring one same-type instance does not affect the others. */
  assert(configure("port", "port", "model-a", 33) == ESP_OK);
  assert(instance_value("rudder") == 7);
  assert(instance_value("port") == 33);
  assert(instance_value("starboard") == 22);
  assert(sensor_provider_handle_control("port", "{}", 2) == ESP_OK);
  assert(control_actions[0] == 1);

  const char *identity_name;
  const char *identity_type;
  assert(registry_provider_identity("starboard", &identity_name,
                                    &identity_type));
  assert(!strcmp(identity_name, "starboard"));
  assert(!strcmp(identity_type, "model-a2"));
  assert(!registry_provider_identity("missing", &identity_name,
                                     &identity_type));

  size_t one_length = registry_provider_json_dump("port", json, sizeof(json));
  assert(one_length == strlen(json));
  cJSON *one = cJSON_Parse(json);
  assert(cJSON_GetObjectItem(one, "value")->valueint == 33);
  cJSON_Delete(one);

  json_gen_str_t active_generator;
  json_gen_str_start(&active_generator, json, sizeof(json), NULL, NULL);
  assert(json_gen_start_array(&active_generator) == 0);
  assert(sensor_active_providers_json_add(&active_generator));
  assert(json_gen_end_array(&active_generator) == 0);
  assert(json_gen_str_end(&active_generator) > 1);
  one = cJSON_Parse(json);
  assert(cJSON_GetArraySize(one) == 4);
  cJSON_Delete(one);

  size_t length = registry_providers_json_dump(json, sizeof(json));
  assert(length == strlen(json));
  cJSON *dump = cJSON_Parse(json);
  assert(cJSON_GetArraySize(dump) == 4);
  cJSON_Delete(dump);
  assert(registry_providers_json_dump(json, length + 1U) == length);
  assert(registry_providers_json_dump(json, length) == 0U);

  /* All four independent snapshots restore without calling configure. */
  reboot();
  assert(registry_init_on_boot() == ESP_OK);
  assert(sensor_provider_count() == 4U);
  assert(validations[0] == 0 && validations[1] == 0);
  assert(starts[0] == 3 && starts[1] == 1);
  assert(instance_value("rudder") == 7);
  assert(instance_value("port") == 33);
  assert(instance_value("starboard") == 22);

  /* Failed validation and startup restore only the targeted instance. */
  const int writes_before = writes;
  assert(configure("clock", "clock", "model-b", -1) == ESP_ERR_INVALID_ARG);
  assert(instance_value("clock") == 60 && writes == writes_before);
  fail_start = true;
  assert(add("failed", "model-b", 8) == ESP_FAIL);
  fail_start = false;
  assert(sensor_provider_count() == 4U &&
         instance_index_for_name("failed") == SIZE_MAX);

  /* A persistence failure leaves the live instance visible for retry. */
  fail_commit = true;
  assert(add("live_only", "model-b", 9) == ESP_FAIL);
  assert(sensor_provider_count() == 5U && instance_value("live_only") == 9);
  fail_commit = false;
  assert(configure("live_only", "live_only", "model-b", 9) == ESP_OK);

  for (int i = 0; i < 5; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "extra%d", i);
    assert(add(name, "model-b", i) == ESP_OK);
  }
  assert(sensor_provider_count() == SENSOR_PROVIDER_MAX_INSTANCES);
  assert(add("overflow", "model-b", 1) == ESP_ERR_NO_MEM);

  /* Truncated storage reads remain atomic. */
  const size_t full_size = stored_size;
  feature_entry_t *first = active_lookup->configurations[0];
  for (size_t n = 0; n < full_size; ++n) {
    stored_size = n;
    assert(registry_read(active_lookup) != ESP_OK);
    assert(active_lookup->count == SENSOR_PROVIDER_MAX_INSTANCES &&
           active_lookup->configurations[0] == first);
  }
  stored_size = full_size;

  /* Unknown types and incompatible binary snapshots fail before startup. */
  runtime_clear();
  registry_clear(active_lookup);
  int snapshot = 42;
  active_lookup->configurations[0] =
      registry_entry_create("broken", "missing", &snapshot, sizeof(snapshot));
  active_lookup->count = 1;
  assert(registry_write(active_lookup) == ESP_OK);
  registry_clear(active_lookup);
  active_lookup = NULL;
  assert(registry_init_on_boot() == ESP_ERR_INVALID_STATE);
  assert(active_lookup == NULL);

  registry_t *lookup = registry_init();
  char bad_snapshot = 1;
  lookup->configurations[0] =
      registry_entry_create("broken", "model-a", &bad_snapshot, 1);
  lookup->count = 1;
  assert(registry_write(lookup) == ESP_OK);
  registry_clear(lookup);
  assert(registry_init_on_boot() == ESP_ERR_INVALID_STATE);

  /* An existing empty registry is complete; defaults are not synthesized. */
  lookup = registry_init();
  assert(registry_write(lookup) == ESP_OK);
  assert(registry_init_on_boot() == ESP_OK);
  assert(sensor_provider_count() == 0U);

  free(stored);
  free(pending);
  puts("Registry instance lifecycle tests passed");
}
