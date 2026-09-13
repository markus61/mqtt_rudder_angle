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

    /* Overlay the angle-sensor members of a parsed configuration document. */
    void angle_sensor_config_apply_json(const cJSON *configuration);

#ifdef __cplusplus
}
#endif
