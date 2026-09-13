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
extern "C"
{
#endif

    /** Registry-owned identity and opaque snapshot for one attached sensor. */
    typedef struct feature_entry
    {
        char *name;
        char *type;
        void *configuration;
        size_t configuration_size;
    } feature_entry_t;

    /** Owned entries; provider callbacks and runtime handles are never persisted. */
    typedef struct
    {
        feature_entry_t **configurations;
        size_t capacity;
        size_t count;
    } registry_t;

    /** Write every registered sensor configuration, including its name, as a JSON array. */
    size_t registry_features_json_dump(char *buffer, size_t buffer_size);

    /** Append attached feature objects to an already-open JSON array. */
    bool registry_features_json_add(json_gen_str_t *json);

    /** Initialize the sensor configuration registry during boot. */
    esp_err_t registry_init_on_boot(void);

    /** Configure a sensor based on an MQTT action JSON object. */
    esp_err_t sensor_config_from_mqtt(const cJSON *action_json);

#ifdef __cplusplus
}
#endif
