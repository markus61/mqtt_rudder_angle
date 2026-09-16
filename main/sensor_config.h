/**
 * @file sensor_config.h
 * @brief Sensor configuration management interface.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"
#include "json_generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Registry-owned identity and opaque snapshot for one attached sensor. */
typedef struct feature_entry {
  char *name;
  char *type;
  void *configuration;
  size_t configuration_size;
} feature_entry_t;

/** Owned entries; provider callbacks and runtime handles are never persisted.
 */
typedef struct {
  feature_entry_t **configurations;
  size_t capacity;
  size_t count;
} registry_t;

/** Write every registered sensor configuration, including its name, as a JSON
 * array. */
size_t registry_providers_json_dump(char *buffer, size_t buffer_size);

/** Write the registered configuration for one provider as a JSON object. */
size_t registry_provider_json_dump(const char *provider_name, char *buffer,
                                  size_t buffer_size);

/** Look up the configured name and type for one provider. */
bool registry_provider_identity(const char *provider_name, const char **name,
                                const char **type);

/** Append attached feature objects to an already-open JSON array. */
bool registry_providers_json_add(json_gen_str_t *json);

/** Initialize persisted sensor settings and start missing providers from
 * defaults, assigning each successful start a boot-session sensor number. */
esp_err_t registry_init_on_boot(void);

/** Configure a sensor based on an MQTT action JSON object. */
esp_err_t sensor_config_from_mqtt(const cJSON *action_json);

/** Number of compiled-in providers that expose a control channel. */
size_t sensor_provider_count(void);

/** Internal provider API name. It is never exposed in an MQTT topic. */
const char *sensor_provider_name(size_t index);

/** Startup-assigned external sensor number, or zero when it did not start. */
size_t sensor_provider_number(const char *provider_name);

/** Deliver a control payload to the provider named by its control topic.
 * Returns the provider's result, or ESP_ERR_INVALID_ARG for an invalid name
 * or payload. */
esp_err_t sensor_provider_handle_control(const char *provider_name,
                                         const char *payload,
                                         int payload_length);

/** Append the numbers of providers whose start operation succeeded. */
bool sensor_active_providers_json_add(json_gen_str_t *json);

/** Append all compiled-in providers and their supported model types. */
bool sensor_available_providers_json_add(json_gen_str_t *json);

/** Encode the catalogue of compiled-in providers as a JSON document. */
size_t sensor_available_providers_json_dump(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
