/** Sensor-independent attachment, persistence and dispatch. */
#include "sensor_config.h"

#include <limits.h>
#include <string.h>

#include "elobau_angle_sensor.h"
#include "esp_log.h"
#include "json_utils.h"
#include "registry_read_write.h"
#include "uptime_sensor.h"

/* Add a provider's public header above and SENSOR_PROVIDER(prefix) below.
 * This catalogue describes available code, not attached hardware.
 * NVS/MQTT supplies attachments. The public APIs expose singleton providers:
 * one configuration and task per provider, irrespective of supported models. */
typedef struct {
  const char *name;
  bool (*can_serve_type)(const char *type);
  size_t (*supported_type_count)(void);
  const char *(*supported_type)(size_t index);
  const void *(*config_get)(void);
  size_t (*config_size)(void);
  bool (*configure)(const cJSON *configuration);
  void (*config_restore)(const void *configuration);
  esp_err_t (*start)(void);
  bool (*config_to_json)(json_gen_str_t *json);
  esp_err_t (*control_action)(const char *payload, int payload_length);
} sensor_provider_t;

#define SENSOR_PROVIDER(prefix)                                                \
  {#prefix, prefix##_can_serve_type, prefix##_supported_type_count,           \
   prefix##_supported_type, prefix##_config_get, prefix##_config_size,         \
   prefix##_configure,      prefix##_config_restore, prefix##_start,           \
   prefix##_config_to_json, prefix##_control_action}

static const sensor_provider_t providers[] = {
    SENSOR_PROVIDER(angle_sensor),
    SENSOR_PROVIDER(uptime_sensor),
};
static bool provider_is_started[sizeof(providers) / sizeof(providers[0])];

static const char *TAG = "sensor_config";
static registry_t *active_lookup;

size_t sensor_provider_count(void) {
  return sizeof(providers) / sizeof(providers[0]);
}

const char *sensor_provider_name(size_t index) {
  return index < sensor_provider_count() ? providers[index].name : NULL;
}

static size_t provider_index(const sensor_provider_t *provider) {
  return (size_t)(provider - providers);
}

bool sensor_active_providers_json_add(json_gen_str_t *json) {
  if (json == NULL) {
    return false;
  }
  for (size_t i = 0; i < sensor_provider_count(); ++i) {
    if (provider_is_started[i] &&
        json_gen_arr_set_string(json, providers[i].name) != 0) {
      return false;
    }
  }
  return true;
}

bool sensor_available_providers_json_add(json_gen_str_t *json) {
  if (json == NULL) {
    return false;
  }
  for (size_t i = 0; i < sensor_provider_count(); ++i) {
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
  if (provider_name == NULL || payload == NULL || payload_length < 0) {
    return ESP_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < sensor_provider_count(); ++i) {
    if (strcmp(provider_name, providers[i].name) == 0) {
      return providers[i].control_action(payload, payload_length);
    }
  }
  return ESP_ERR_INVALID_ARG;
}

/* Reject ambiguous claims as well as unknown models. */
static const sensor_provider_t *provider_for_type(const char *type) {
  const sensor_provider_t *match = NULL;
  for (size_t i = 0; i < sizeof(providers) / sizeof(providers[0]); ++i) {
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

static const sensor_provider_t *provider_for_name(const char *name) {
  if (name == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < sensor_provider_count(); ++i) {
    if (strcmp(name, providers[i].name) == 0) {
      return &providers[i];
    }
  }
  return NULL;
}

bool registry_providers_json_add(json_gen_str_t *generator) {
  if (generator == NULL) {
    return false;
  }
  for (size_t i = 0; active_lookup != NULL && i < active_lookup->count; ++i) {
    const feature_entry_t *entry = active_lookup->configurations[i];
    const sensor_provider_t *provider = provider_for_type(entry->type);
    if (provider == NULL ||
        entry->configuration_size != provider->config_size() ||
        json_gen_start_object(generator) != 0 ||
        !json_obj_set_escaped_string(generator, "name", entry->name) ||
        !json_obj_set_escaped_string(generator, "type", entry->type) ||
        !provider->config_to_json(generator) ||
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
  return length <= 1 || (size_t)length > buffer_size ? 0U : (size_t)length - 1U;
}

size_t registry_provider_json_dump(const char *provider_name, char *buffer,
                                  size_t buffer_size) {
  const sensor_provider_t *provider = provider_for_name(provider_name);
  if (provider == NULL || buffer == NULL || buffer_size < 3U ||
      buffer_size > INT_MAX) {
    return 0U;
  }

  json_gen_str_t generator;
  json_gen_str_start(&generator, buffer, (int)buffer_size, NULL, NULL);
  if (json_gen_start_object(&generator) != 0) {
    return 0U;
  }

  for (size_t i = 0; active_lookup != NULL && i < active_lookup->count; ++i) {
    const feature_entry_t *entry = active_lookup->configurations[i];
    if (provider_for_type(entry->type) != provider) {
      continue;
    }
    if (entry->configuration_size != provider->config_size() ||
        !json_obj_set_escaped_string(&generator, "name", entry->name) ||
        !json_obj_set_escaped_string(&generator, "type", entry->type) ||
        !provider->config_to_json(&generator)) {
      return 0U;
    }
    break;
  }

  if (json_gen_end_object(&generator) != 0) {
    return 0U;
  }
  const int length = json_gen_str_end(&generator);
  return length <= 1 || (size_t)length > buffer_size ? 0U
                                                       : (size_t)length - 1U;
}

bool registry_provider_identity(const char *provider_name, const char **name,
                                const char **type) {
  const sensor_provider_t *provider = provider_for_name(provider_name);
  if (provider == NULL || name == NULL || type == NULL) {
    return false;
  }
  for (size_t i = 0; active_lookup != NULL && i < active_lookup->count; ++i) {
    const feature_entry_t *entry = active_lookup->configurations[i];
    if (provider_for_type(entry->type) == provider &&
        entry->configuration_size == provider->config_size()) {
      *name = entry->name;
      *type = entry->type;
      return true;
    }
  }
  return false;
}

esp_err_t registry_init_on_boot(void) {
  if (active_lookup != NULL) {
    return ESP_OK;
  }
  memset(provider_is_started, 0, sizeof(provider_is_started));
  active_lookup = registry_init();
  esp_err_t err = registry_read(active_lookup);
  if (err != ESP_OK) {
    return err;
  }

  /* Check routing and binary layout for every record before touching any
   * provider. Persisted settings bypass MQTT validation/configure(). */
  for (size_t i = 0; i < active_lookup->count; ++i) {
    const feature_entry_t *entry = active_lookup->configurations[i];
    const sensor_provider_t *provider = provider_for_type(entry->type);
    if (provider == NULL ||
        entry->configuration_size != provider->config_size()) {
      ESP_LOGE(TAG, "Cannot restore provider for '%s' (%s)", entry->name,
               entry->type);
      registry_clear(active_lookup);
      return ESP_ERR_INVALID_STATE;
    }
    for (size_t j = 0; j < i; ++j) {
      if (provider_for_type(active_lookup->configurations[j]->type) ==
          provider) {
        registry_clear(active_lookup);
        return ESP_ERR_INVALID_STATE;
      }
    }
  }

  esp_err_t result = ESP_OK;
  for (size_t i = 0; i < active_lookup->count; ++i) {
    const feature_entry_t *entry = active_lookup->configurations[i];
    const sensor_provider_t *provider = provider_for_type(entry->type);
    provider->config_restore(entry->configuration);
    err = provider->start();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Could not start '%s': %s", entry->name,
               esp_err_to_name(err));
      result = err;
    } else {
      provider_is_started[provider_index(provider)] = true;
    }
  }
  return result;
}

/* Called by the MQTT event task after boot; registry mutations are serialized
 * there. Copy names/types before the caller deletes the cJSON action. */
esp_err_t sensor_config_from_mqtt(const cJSON *action_json) {
  const char *type = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(action_json, "type"));
  const char *name = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(action_json, "name"));
  if (!cJSON_IsObject(action_json) || type == NULL || type[0] == '\0' ||
      name == NULL || name[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  if (active_lookup == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  const sensor_provider_t *provider = provider_for_type(type);
  if (provider == NULL) {
    ESP_LOGW(TAG, "Unsupported or ambiguous sensor type '%s'", type);
    return ESP_ERR_INVALID_ARG;
  }

  size_t index = active_lookup->count;
  for (size_t i = 0; i < active_lookup->count; ++i) {
    const feature_entry_t *entry = active_lookup->configurations[i];
    if (strcmp(entry->name, name) == 0) {
      /* Reconfigure by identity, without repurposing a running provider. */
      if (strcmp(entry->type, type) != 0) {
        return ESP_ERR_INVALID_STATE;
      }
      index = i;
    } else if (provider_for_type(entry->type) == provider) {
      ESP_LOGW(TAG, "Provider already attached as '%s'", entry->name);
      return ESP_ERR_INVALID_STATE;
    }
  }
  const bool adding = index == active_lookup->count;
  if (adding && active_lookup->count == active_lookup->capacity) {
    return ESP_ERR_NO_MEM;
  }

  const size_t size = provider->config_size();
  feature_entry_t *candidate =
      registry_entry_create(name, type, provider->config_get(), size);
  if (candidate == NULL) {
    return ESP_ERR_NO_MEM;
  }
  /* Initially the candidate holds a rollback copy of live provider settings. */
  if (!provider->configure(action_json)) {
    provider->config_restore(candidate->configuration);
    registry_entry_free(candidate);
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = provider->start();
  if (err != ESP_OK) {
    provider->config_restore(candidate->configuration);
    registry_entry_free(candidate);
    return err;
  }
  provider_is_started[provider_index(provider)] = true;

  memcpy(candidate->configuration, provider->config_get(), size);
  feature_entry_t *previous =
      adding ? NULL : active_lookup->configurations[index];
  active_lookup->configurations[index] = candidate;
  if (adding) {
    ++active_lookup->count;
  }
  registry_entry_free(previous);

  /* A started attachment exists even if NVS fails. Keep it visible so the
   * same MQTT action can retry persistence; the public API has no stop(). */
  err = registry_write(active_lookup);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "'%s' is running but not persisted: %s", name,
             esp_err_to_name(err));
  }
  return err;
}
