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

typedef enum {
  SENSOR_PROVIDER_ACTIVE = 0,
  SENSOR_PROVIDER_INACTIVE = 1,
} sensor_provider_state_t;

/** Registry-owned identity, state, and opaque snapshot for one sensor. */
typedef struct feature_entry {
  char *name;
  char *type;
  sensor_provider_state_t state;
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

/** Maximum bytes, including the terminator, in an attached sensor name.
 * Sensor names are MQTT topic levels: ASCII letters, digits, '_' and '-' only. */
#define SENSOR_CONFIG_NAME_SIZE 64U
#define SENSOR_PROVIDER_MAX_INSTANCES 10U

/** Write every registered sensor configuration, including its name, as a JSON
 * array. */
size_t registry_providers_json_dump(char *buffer, size_t buffer_size);

/** Write one named instance's live configuration as a JSON object. */
size_t registry_provider_json_dump(const char *provider_name, char *buffer,
                                  size_t buffer_size);

/** Look up one instance's registry-owned name and type. */
bool registry_provider_identity(const char *provider_name, const char **name,
                                const char **type);

/** Return one instance's persisted state, or false when its name is unknown. */
bool registry_provider_state(const char *provider_name,
                             sensor_provider_state_t *state);

/** Append attached feature objects to an already-open JSON array. */
bool registry_providers_json_add(json_gen_str_t *json);

/** Restore and start the complete persisted registry. When no registry exists,
 * create one numeric-name instance from every provider's default and persist
 * that initial registry. */
esp_err_t registry_init_on_boot(void);

/** Configure the instance addressed by provider_name from an MQTT action.
 * An omitted action name retains that identity; a supplied name can rename it.
 * Hardware type is fixed; an optional type can only repeat its current type. */
esp_err_t sensor_provider_configure_from_mqtt(const char *provider_name,
                                              const cJSON *action_json);

/** Create, configure, start and persist an additional provider instance. */
esp_err_t sensor_provider_add_from_mqtt(const cJSON *action_json);

/** Start and persist the inactive instance named by a device MQTT action. */
esp_err_t sensor_provider_activate_from_mqtt(const cJSON *action_json);

/** Number of registered provider instances, active and inactive. */
size_t sensor_provider_count(void);

/** Name of a registered provider instance. */
const char *sensor_provider_name(size_t index);

/** One-based registry position for a started named instance, or zero. */
size_t sensor_provider_number(const char *provider_name);

/** Copy a started instance's name into the MQTT control/reply component. */
bool sensor_provider_control_component(const char *provider_name, char *buffer,
                                       size_t buffer_size);

/** Deliver a control payload to the instance named by its control topic. */
esp_err_t sensor_provider_handle_control(const char *provider_name,
                                         const char *payload,
                                         int payload_length);

/** Append every started instance and its normal publication topics. */
bool sensor_active_providers_json_add(json_gen_str_t *json);

/** Append every inactive registry entry and its identity. */
bool sensor_inactive_providers_json_add(json_gen_str_t *json);

/** Append all compiled-in providers and their supported model types. */
bool sensor_available_providers_json_add(json_gen_str_t *json);

/** Encode the catalogue of compiled-in providers as a JSON document. */
size_t sensor_available_providers_json_dump(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
