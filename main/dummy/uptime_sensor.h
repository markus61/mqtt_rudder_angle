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

/** Validate and apply an uptime-sensor configuration action. */
bool uptime_sensor_configure(const cJSON *configuration);

/** Restore a trusted record of config_size() bytes; no MQTT validation. */
void uptime_sensor_config_restore(const void *configuration);

/** Append provider settings (excluding name/type) from an opaque snapshot. */
bool uptime_sensor_config_to_json(json_gen_str_t *json);

/** Parse and dispatch an MQTT control-action JSON payload. */
void uptime_sensor_control_action(const char *payload, int payload_length);

#ifdef __cplusplus
}
#endif
