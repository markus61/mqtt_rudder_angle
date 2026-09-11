#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ANGLE_SENSOR_CONFIG_TOPIC_SIZE 64
#define ANGLE_SENSOR_TYPE_SIZE 32

    typedef struct
    {
        /* GPIO the angle sensor is wired to, or -1 while still unknown. */
        int sensor_gpio_number;
        int sensor_sample_period_ms;
        int sensor_samples_per_reading;
        char sensor_topic[ANGLE_SENSOR_CONFIG_TOPIC_SIZE];
        char sensor_type[ANGLE_SENSOR_TYPE_SIZE];
    } angle_sensor_config_t;

    const angle_sensor_config_t *angle_sensor_config_get(void);

    /* Check that a complete angle-sensor configuration action has valid members. */
    bool angle_sensor_configure(const cJSON *configuration);

    /* Restore a previously validated configuration record from NVS. */
    void angle_sensor_config_restore(const angle_sensor_config_t *configuration);

    /* Overlay the angle-sensor members of a parsed configuration document. */
    void angle_sensor_config_apply_json(const cJSON *configuration);

    /** Write this sensor's braindump feature object and return its length, or 0 on failure. */
    size_t angle_sensor_config_format_feature_json(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
