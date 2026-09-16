/**
 * @file uptime_sensor.h
 * @brief Interface for the dummy uptime sensor provider.
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

/** Start periodic uptime publication using the configured interval. */
esp_err_t uptime_sensor_start(void);

/** Return the opaque configuration record persisted for this provider. */
const void *uptime_sensor_config_get(void);

/** Return the size of the provider's opaque persistent configuration record. */
size_t uptime_sensor_config_size(void);

/** Return whether this provider supports the supplied model type. */
bool uptime_sensor_can_serve_type(const char *type);

/** Return the number of model types supported by this provider. */
size_t uptime_sensor_supported_type_count(void);

/** Return a supported model type by index, or NULL when out of range. */
const char *uptime_sensor_supported_type(size_t index);

/** Validate and apply an uptime-sensor configuration action atomically. */
esp_err_t uptime_sensor_configure(const cJSON *configuration);

/** Restore a trusted record of config_size() bytes; no MQTT validation. */
esp_err_t uptime_sensor_config_restore(const void *configuration);

/** Append provider settings (excluding name/type) to an open JSON object. */
esp_err_t uptime_sensor_config_to_json(json_gen_str_t *json);

/** Parse and dispatch an MQTT control-action JSON payload.
 * Returns ESP_OK when handled, otherwise an error describing rejection. */
esp_err_t uptime_sensor_control_action(const char *payload, int payload_length);

#ifdef __cplusplus
}
#endif
