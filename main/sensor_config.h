/**
 * @file sensor_config.h
 * @brief Sensor configuration management interface.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** Identity and configuration for one configured sensor. */
    typedef struct feature_entry
    {
        const char *name;
        const char *type;
        void *configuration;
        size_t configuration_size;
    } feature_entry_t;

    /** A non-owning array of configurations returned by a lookup. */
    typedef struct
    {
        feature_entry_t *const *items;
        size_t count;
    } registry_lookup_result_t;

    /** Result of a lookup in the sensor configuration registry. */
    typedef struct
    {
        feature_entry_t **configurations;
        size_t capacity;
        size_t count;
        feature_entry_t **matches;
        size_t match_capacity;
    } registry_t;

    /** Write every registered sensor configuration, including its name, as a JSON array. */
    size_t registry_features_json_dump(char *buffer, size_t buffer_size);

    /** Initialize the sensor configuration registry during boot. */
    esp_err_t registry_init_on_boot(void);

    /** Configure a sensor based on an MQTT action JSON object. */
    esp_err_t sensor_config_from_mqtt(const cJSON *action_json);

#ifdef __cplusplus
}
#endif
