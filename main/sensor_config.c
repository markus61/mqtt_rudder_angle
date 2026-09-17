/** Sensor-independent provider factories, instances, persistence and dispatch. */
#include "sensor_config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "elobau_angle_sensor.h"
#include "esp_log.h"
#include "json_utils.h"
#include "nvs.h"
#include "registry_read_write.h"
#include "uptime_sensor.h"

typedef struct {
  const char *name;
  bool (*can_serve_type)(const char *type);
  size_t (*supported_type_count)(void);
  const char *(*supported_type)(size_t index);
  void *(*create)(const char *type);
  void (*destroy)(void *instance);
  const void *(*config_get)(const void *instance);
  size_t (*config_size)(void);
  esp_err_t (*configure)(void *instance, const cJSON *configuration);
  esp_err_t (*config_restore)(void *instance, const void *configuration);
  esp_err_t (*start)(void *instance);
  esp_err_t (*config_to_json)(const void *instance, json_gen_str_t *json);
  esp_err_t (*working_topics_json_add)(const void *instance,
                                       json_gen_str_t *json);
  esp_err_t (*control_action)(void *instance, const char *instance_name,
                              const char *payload, int payload_length);
} sensor_provider_t;

#define SENSOR_PROVIDER(prefix)                                                \
  {#prefix, prefix##_can_serve_type, prefix##_supported_type_count,           \
   prefix##_supported_type, prefix##_create, prefix##_destroy,                \
   prefix##_config_get, prefix##_config_size, prefix##_configure,             \
   prefix##_config_restore, prefix##_start, prefix##_config_to_json,          \
   prefix##_working_topics_json_add, prefix##_control_action}

static const sensor_provider_t providers[] = {
    SENSOR_PROVIDER(angle_sensor),
    SENSOR_PROVIDER(uptime_sensor),
};

typedef struct {
  const sensor_provider_t *provider;
  void *instance;
  bool started;
} sensor_runtime_t;

static sensor_runtime_t runtimes[SENSOR_PROVIDER_MAX_INSTANCES];
static registry_t *active_lookup;
static const char *TAG = "sensor_config";

static const char *state_name(sensor_provider_state_t state) {
  return state == SENSOR_PROVIDER_ACTIVE ? "active" : "inactive";
}

static size_t provider_catalogue_count(void) {
  return sizeof(providers) / sizeof(providers[0]);
}

static void runtime_clear(void) {
  for (size_t i = 0; i < SENSOR_PROVIDER_MAX_INSTANCES; ++i) {
    if (runtimes[i].instance != NULL && runtimes[i].provider != NULL) {
      runtimes[i].provider->destroy(runtimes[i].instance);
    }
  }
  memset(runtimes, 0, sizeof(runtimes));
}

size_t sensor_provider_count(void) {
  return active_lookup != NULL ? active_lookup->count : 0U;
}

const char *sensor_provider_name(size_t index) {
  return active_lookup != NULL && index < active_lookup->count
             ? active_lookup->configurations[index]->name
             : NULL;
}

size_t sensor_provider_number(const char *provider_name) {
  if (provider_name == NULL || active_lookup == NULL) {
    return 0U;
  }
  for (size_t i = 0; i < active_lookup->count; ++i) {
    if (runtimes[i].started &&
        strcmp(provider_name, active_lookup->configurations[i]->name) == 0) {
      return i + 1U;
    }
  }
  return 0U;
}

static const sensor_provider_t *provider_for_type(const char *type) {
  if (type == NULL) {
    return NULL;
  }
  const sensor_provider_t *match = NULL;
  for (size_t i = 0; i < provider_catalogue_count(); ++i) {
    if (providers[i].can_serve_type(type)) {
      if (match != NULL) {
        ESP_LOGE(TAG, "Multiple providers claim type '%s'", type);
        return NULL;
      }
      match = &providers[i];
    }
  }
  return match;
}

static size_t instance_index_for_name(const char *name) {
  if (name == NULL || active_lookup == NULL) {
    return SIZE_MAX;
  }
  for (size_t i = 0; i < active_lookup->count; ++i) {
    if (strcmp(name, active_lookup->configurations[i]->name) == 0) {
      return i;
    }
  }
  return SIZE_MAX;
}

static bool sensor_name_is_safe_topic_level(const char *name) {
  if (name == NULL || name[0] == '\0' ||
      strlen(name) >= SENSOR_CONFIG_NAME_SIZE) {
    return false;
  }
  for (const unsigned char *character = (const unsigned char *)name;
       *character != '\0'; ++character) {
    if (!((*character >= 'A' && *character <= 'Z') ||
          (*character >= 'a' && *character <= 'z') ||
          (*character >= '0' && *character <= '9') || *character == '_' ||
          *character == '-')) {
      return false;
    }
  }
  return true;
}

bool sensor_active_providers_json_add(json_gen_str_t *json) {
  if (json == NULL) {
    return false;
  }
  for (size_t i = 0; i < sensor_provider_count(); ++i) {
    if (!runtimes[i].started) {
      continue;
    }
    const feature_entry_t *entry = active_lookup->configurations[i];
    if (json_gen_start_object(json) != 0 ||
        !json_obj_set_escaped_string(json, "name", entry->name) ||
        json_gen_push_array(json, "working_topics") != 0 ||
        runtimes[i].provider->working_topics_json_add(runtimes[i].instance,
                                                       json) != ESP_OK ||
        json_gen_pop_array(json) != 0 || json_gen_end_object(json) != 0) {
      return false;
    }
  }
  return true;
}

bool sensor_inactive_providers_json_add(json_gen_str_t *json) {
  if (json == NULL) {
    return false;
  }
  for (size_t i = 0; i < sensor_provider_count(); ++i) {
    if (runtimes[i].started) {
      continue;
    }
    const feature_entry_t *entry = active_lookup->configurations[i];
    if (json_gen_start_object(json) != 0 ||
        !json_obj_set_escaped_string(json, "name", entry->name) ||
        !json_obj_set_escaped_string(json, "type", entry->type) ||
        json_gen_end_object(json) != 0) {
      return false;
    }
  }
  return true;
}

bool sensor_available_providers_json_add(json_gen_str_t *json) {
  if (json == NULL) {
    return false;
  }
  for (size_t i = 0; i < provider_catalogue_count(); ++i) {
    if (json_gen_start_object(json) != 0 ||
        !json_obj_set_escaped_string(json, "name", providers[i].name) ||
        json_gen_push_array(json, "types") != 0) {
      return false;
    }
    for (size_t type_index = 0;
         type_index < providers[i].supported_type_count(); ++type_index) {
      const char *type = providers[i].supported_type(type_index);
      if (type == NULL || json_gen_arr_set_string(json, type) != 0) {
        return false;
      }
    }
    if (json_gen_pop_array(json) != 0 || json_gen_end_object(json) != 0) {
      return false;
    }
  }
  return true;
}

size_t sensor_available_providers_json_dump(char *buffer, size_t buffer_size) {
  if (buffer == NULL || buffer_size < 3U || buffer_size > INT_MAX) {
    return 0U;
  }
  json_gen_str_t generator;
  json_gen_str_start(&generator, buffer, (int)buffer_size, NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      json_gen_push_array(&generator, "providers") != 0 ||
      !sensor_available_providers_json_add(&generator) ||
      json_gen_pop_array(&generator) != 0 ||
      json_gen_end_object(&generator) != 0) {
    return 0U;
  }
  const int length = json_gen_str_end(&generator);
  return length <= 1 || (size_t)length > buffer_size ? 0U
                                                      : (size_t)length - 1U;
}

esp_err_t sensor_provider_handle_control(const char *provider_name,
                                         const char *payload,
                                         int payload_length) {
  if (payload == NULL || payload_length < 0) {
    return ESP_ERR_INVALID_ARG;
  }
  const size_t index = instance_index_for_name(provider_name);
  if (index == SIZE_MAX || !runtimes[index].started) {
    return ESP_ERR_INVALID_ARG;
  }
  char provider_name_copy[SENSOR_CONFIG_NAME_SIZE];
  strlcpy(provider_name_copy, active_lookup->configurations[index]->name,
          sizeof(provider_name_copy));
  cJSON *action_json = cJSON_ParseWithLength(payload, (size_t)payload_length);
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(action_json, "action");
  const bool deactivating = cJSON_IsString(action) && action->valuestring != NULL &&
                            strcasecmp(action->valuestring, "deactivate") == 0;
  const bool removing = cJSON_IsString(action) && action->valuestring != NULL &&
                        strcasecmp(action->valuestring, "remove") == 0;
  cJSON_Delete(action_json);
  if (deactivating || removing) {
    runtimes[index].provider->destroy(runtimes[index].instance);
    if (removing) {
      registry_entry_free(active_lookup->configurations[index]);
      const size_t trailing_count = active_lookup->count - index - 1U;
      if (trailing_count > 0U) {
        memmove(&active_lookup->configurations[index],
                &active_lookup->configurations[index + 1U],
                trailing_count * sizeof(active_lookup->configurations[0]));
        memmove(&runtimes[index], &runtimes[index + 1U],
                trailing_count * sizeof(runtimes[0]));
      }
      --active_lookup->count;
      active_lookup->configurations[active_lookup->count] = NULL;
      runtimes[active_lookup->count] = (sensor_runtime_t){0};
    } else {
      runtimes[index].instance = NULL;
      runtimes[index].started = false;
      active_lookup->configurations[index]->state = SENSOR_PROVIDER_INACTIVE;
    }
    const esp_err_t err = registry_write(active_lookup);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "'%s' is %s but not persisted: %s", provider_name_copy,
               removing ? "removed" : "inactive",
               esp_err_to_name(err));
    }
    return err;
  }
  char instance_name[SENSOR_CONFIG_NAME_SIZE];
  strlcpy(instance_name, active_lookup->configurations[index]->name,
          sizeof(instance_name));
  return runtimes[index].provider->control_action(
      runtimes[index].instance, instance_name, payload, payload_length);
}

static esp_err_t entry_config_to_json(size_t index, json_gen_str_t *generator) {
  const feature_entry_t *entry = active_lookup->configurations[index];
  const sensor_provider_t *provider = runtimes[index].provider;
  if (provider == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (runtimes[index].instance != NULL) {
    return provider->config_to_json(runtimes[index].instance, generator);
  }
  void *instance = provider->create(entry->type);
  if (instance == NULL) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = provider->config_restore(instance, entry->configuration);
  if (err == ESP_OK) {
    err = provider->config_to_json(instance, generator);
  }
  provider->destroy(instance);
  return err;
}

bool registry_providers_json_add(json_gen_str_t *generator) {
  if (generator == NULL) {
    return false;
  }
  for (size_t i = 0; i < sensor_provider_count(); ++i) {
    const feature_entry_t *entry = active_lookup->configurations[i];
    if (runtimes[i].provider == NULL ||
        entry->configuration_size != runtimes[i].provider->config_size() ||
        json_gen_start_object(generator) != 0 ||
        !json_obj_set_escaped_string(generator, "name", entry->name) ||
        !json_obj_set_escaped_string(generator, "type", entry->type) ||
        !json_obj_set_escaped_string(generator, "state", state_name(entry->state)) ||
        entry_config_to_json(i, generator) != ESP_OK ||
        json_gen_end_object(generator) != 0) {
      return false;
    }
  }
  return true;
}

size_t registry_providers_json_dump(char *buffer, size_t buffer_size) {
  if (buffer == NULL || buffer_size < 3U || buffer_size > INT_MAX) {
    return 0U;
  }
  json_gen_str_t generator;
  json_gen_str_start(&generator, buffer, (int)buffer_size, NULL, NULL);
  if (json_gen_start_array(&generator) != 0 ||
      !registry_providers_json_add(&generator) ||
      json_gen_end_array(&generator) != 0) {
    return 0U;
  }
  const int length = json_gen_str_end(&generator);
  return length <= 1 || (size_t)length > buffer_size ? 0U
                                                      : (size_t)length - 1U;
}

size_t registry_provider_json_dump(const char *provider_name, char *buffer,
                                   size_t buffer_size) {
  const size_t index = instance_index_for_name(provider_name);
  if (index == SIZE_MAX || buffer == NULL || buffer_size < 3U ||
      buffer_size > INT_MAX) {
    return 0U;
  }
  const feature_entry_t *entry = active_lookup->configurations[index];
  json_gen_str_t generator;
  json_gen_str_start(&generator, buffer, (int)buffer_size, NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "name", entry->name) ||
      !json_obj_set_escaped_string(&generator, "type", entry->type) ||
      !json_obj_set_escaped_string(&generator, "state", state_name(entry->state)) ||
      entry_config_to_json(index, &generator) != ESP_OK ||
      json_gen_end_object(&generator) != 0) {
    return 0U;
  }
  const int length = json_gen_str_end(&generator);
  return length <= 1 || (size_t)length > buffer_size ? 0U
                                                      : (size_t)length - 1U;
}

bool registry_provider_identity(const char *provider_name, const char **name,
                                const char **type) {
  const size_t index = instance_index_for_name(provider_name);
  if (index == SIZE_MAX || name == NULL || type == NULL) {
    return false;
  }
  *name = active_lookup->configurations[index]->name;
  *type = active_lookup->configurations[index]->type;
  return true;
}

bool registry_provider_state(const char *provider_name,
                             sensor_provider_state_t *state) {
  const size_t index = instance_index_for_name(provider_name);
  if (index == SIZE_MAX || state == NULL) {
    return false;
  }
  *state = active_lookup->configurations[index]->state;
  return true;
}

bool sensor_provider_control_component(const char *provider_name, char *buffer,
                                       size_t buffer_size) {
  const size_t index = instance_index_for_name(provider_name);
  if (index == SIZE_MAX || !runtimes[index].started || buffer == NULL) {
    return false;
  }
  const size_t length = strlen(active_lookup->configurations[index]->name);
  if (length + 1U > buffer_size) {
    return false;
  }
  memcpy(buffer, active_lookup->configurations[index]->name, length + 1U);
  return true;
}

static esp_err_t start_restored_registry(void) {
  for (size_t i = 0; i < active_lookup->count; ++i) {
    feature_entry_t *entry = active_lookup->configurations[i];
    const sensor_provider_t *provider = provider_for_type(entry->type);
    if (!sensor_name_is_safe_topic_level(entry->name) || provider == NULL ||
        (entry->state != SENSOR_PROVIDER_ACTIVE &&
         entry->state != SENSOR_PROVIDER_INACTIVE) ||
        entry->configuration_size != provider->config_size()) {
      return ESP_ERR_INVALID_STATE;
    }
  }
  for (size_t i = 0; i < active_lookup->count; ++i) {
    feature_entry_t *entry = active_lookup->configurations[i];
    const sensor_provider_t *provider = provider_for_type(entry->type);
    runtimes[i].provider = provider;
    if (entry->state == SENSOR_PROVIDER_INACTIVE) {
      continue;
    }
    void *instance = provider->create(entry->type);
    if (instance == NULL) {
      return ESP_ERR_NO_MEM;
    }
    runtimes[i].instance = instance;
    esp_err_t err = provider->config_restore(instance, entry->configuration);
    if (err == ESP_OK) {
      err = provider->start(instance);
    }
    if (err != ESP_OK) {
      return err;
    }
    runtimes[i].started = true;
  }
  return ESP_OK;
}

static esp_err_t create_default_registry(void) {
  for (size_t i = 0; i < provider_catalogue_count(); ++i) {
    if (active_lookup->count == active_lookup->capacity) {
      return ESP_ERR_NO_MEM;
    }
    const char *type = providers[i].supported_type(0U);
    void *instance = type != NULL ? providers[i].create(type) : NULL;
    if (instance == NULL) {
      return ESP_ERR_NO_MEM;
    }
    char name[SENSOR_CONFIG_NAME_SIZE];
    const int length = snprintf(name, sizeof(name), "%zu",
                                active_lookup->count + 1U);
    if (length <= 0 || (size_t)length >= sizeof(name)) {
      providers[i].destroy(instance);
      return ESP_ERR_INVALID_SIZE;
    }
    feature_entry_t *entry = registry_entry_create(
        name, type, providers[i].config_get(instance),
        providers[i].config_size());
    if (entry == NULL) {
      providers[i].destroy(instance);
      return ESP_ERR_NO_MEM;
    }
    entry->state = SENSOR_PROVIDER_ACTIVE;
    esp_err_t err = providers[i].start(instance);
    if (err != ESP_OK) {
      registry_entry_free(entry);
      providers[i].destroy(instance);
      return err;
    }
    const size_t index = active_lookup->count++;
    active_lookup->configurations[index] = entry;
    runtimes[index] = (sensor_runtime_t){.provider = &providers[i],
                                         .instance = instance,
                                         .started = true};
  }
  return registry_write(active_lookup);
}

esp_err_t registry_init_on_boot(void) {
  if (active_lookup != NULL) {
    return ESP_OK;
  }
  active_lookup = registry_init();
  runtime_clear();
  esp_err_t err = registry_read(active_lookup);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    registry_clear(active_lookup);
    err = create_default_registry();
  } else if (err == ESP_OK) {
    err = start_restored_registry();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not initialize provider registry: %s",
             esp_err_to_name(err));
    runtime_clear();
    registry_clear(active_lookup);
    active_lookup = NULL;
  }
  return err;
}

esp_err_t sensor_provider_activate_from_mqtt(const cJSON *action_json) {
  if (!cJSON_IsObject(action_json) || active_lookup == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *name = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(action_json, "name"));
  const size_t index = instance_index_for_name(name);
  if (index == SIZE_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  feature_entry_t *entry = active_lookup->configurations[index];
  if (entry->state != SENSOR_PROVIDER_INACTIVE ||
      runtimes[index].instance != NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  const sensor_provider_t *provider = runtimes[index].provider;
  void *instance = provider->create(entry->type);
  if (instance == NULL) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = provider->config_restore(instance, entry->configuration);
  if (err == ESP_OK) {
    err = provider->start(instance);
  }
  if (err != ESP_OK) {
    provider->destroy(instance);
    return err;
  }
  runtimes[index].instance = instance;
  runtimes[index].started = true;
  entry->state = SENSOR_PROVIDER_ACTIVE;
  err = registry_write(active_lookup);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "'%s' is active but not persisted: %s", name,
             esp_err_to_name(err));
  }
  return err;
}

esp_err_t sensor_provider_configure_from_mqtt(const char *provider_name,
                                              const cJSON *action_json) {
  if (!cJSON_IsObject(action_json) || active_lookup == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const size_t index = instance_index_for_name(provider_name);
  if (index == SIZE_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  const cJSON *name_item =
      cJSON_GetObjectItemCaseSensitive(action_json, "name");
  const cJSON *type_item =
      cJSON_GetObjectItemCaseSensitive(action_json, "type");
  const char *requested_type = cJSON_GetStringValue(type_item);
  feature_entry_t *current = active_lookup->configurations[index];
  /* A provider action is already addressed to one unambiguous instance by its
   * MQTT control topic.  A name is therefore optional: omitting it retains the
   * current name, while supplying it explicitly requests a rename. */
  const char *name = name_item != NULL ? cJSON_GetStringValue(name_item)
                                       : current->name;
  if (!sensor_name_is_safe_topic_level(name) ||
      (type_item != NULL && requested_type == NULL) ||
      (requested_type != NULL && strcmp(requested_type, current->type) != 0)) {
    return ESP_ERR_INVALID_ARG;
  }
  const size_t duplicate = instance_index_for_name(name);
  if (duplicate != SIZE_MAX && duplicate != index) {
    return ESP_ERR_INVALID_STATE;
  }

  const sensor_provider_t *provider = runtimes[index].provider;
  feature_entry_t *candidate = registry_entry_create(
      name, current->type, provider->config_get(runtimes[index].instance),
      provider->config_size());
  if (candidate == NULL) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = provider->configure(runtimes[index].instance, action_json);
  if (err == ESP_OK) {
    err = provider->start(runtimes[index].instance);
  }
  if (err != ESP_OK) {
    const esp_err_t restore_err = provider->config_restore(
        runtimes[index].instance, candidate->configuration);
    registry_entry_free(candidate);
    return restore_err != ESP_OK ? restore_err : err;
  }
  memcpy(candidate->configuration, provider->config_get(runtimes[index].instance),
         candidate->configuration_size);
  active_lookup->configurations[index] = candidate;
  registry_entry_free(current);
  err = registry_write(active_lookup);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "'%s' is running but not persisted: %s", candidate->name,
             esp_err_to_name(err));
  }
  return err;
}

esp_err_t sensor_provider_add_from_mqtt(const cJSON *action_json) {
  if (!cJSON_IsObject(action_json) || active_lookup == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *name = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(action_json, "name"));
  const char *type = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(action_json, "type"));
  if (!sensor_name_is_safe_topic_level(name) || type == NULL ||
      type[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  if (instance_index_for_name(name) != SIZE_MAX) {
    return ESP_ERR_INVALID_STATE;
  }
  if (active_lookup->count == active_lookup->capacity) {
    return ESP_ERR_NO_MEM;
  }
  const sensor_provider_t *provider = provider_for_type(type);
  if (provider == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  void *instance = provider->create(type);
  if (instance == NULL) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = provider->configure(instance, action_json);
  if (err == ESP_OK) {
    err = provider->start(instance);
  }
  if (err != ESP_OK) {
    provider->destroy(instance);
    return err;
  }
  feature_entry_t *entry = registry_entry_create(
      name, type, provider->config_get(instance), provider->config_size());
  if (entry == NULL) {
    provider->destroy(instance);
    return ESP_ERR_NO_MEM;
  }
  entry->state = SENSOR_PROVIDER_ACTIVE;
  const size_t index = active_lookup->count++;
  active_lookup->configurations[index] = entry;
  runtimes[index] = (sensor_runtime_t){.provider = provider,
                                       .instance = instance,
                                       .started = true};
  err = registry_write(active_lookup);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "'%s' is running but not persisted: %s", name,
             esp_err_to_name(err));
  }
  return err;
}
