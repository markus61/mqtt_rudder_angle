#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "cJSON.h"
#include "esp_err.h"

/* The value is an array, rather than one key per feature. Its order is the
 * firmware's attached-feature order; append new entries when adding sensors. */
#define FEATURES_CONFIG_NVS_KEY "feature_configs"

#ifdef __cplusplus
extern "C"
{
#endif

/** Restore configuration records for every feature built into this device. */
esp_err_t features_config_load_from_nvs(void);

/** Store configuration records for every feature built into this device. */
esp_err_t features_config_store_to_nvs(void);

/** Validate the payload of a configure_sensor control action. */
bool features_config_validate_sensor_configuration_action(const cJSON *action_json);

/** Apply a configure_sensor action to its feature and start it. */
esp_err_t features_config_configure_sensor(const cJSON *action_json);

/** Write the attached features as a braindump JSON array and return its length. */
size_t features_config_format_json(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
