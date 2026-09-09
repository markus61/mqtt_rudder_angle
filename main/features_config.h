#pragma once

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

#ifdef __cplusplus
}
#endif
