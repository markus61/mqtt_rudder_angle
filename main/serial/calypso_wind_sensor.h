#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float speed_mps;
    float direction_degrees;
} calypso_wind_sensor_reading_t;

/* Start the sensor transport and make readings available. */
esp_err_t calypso_wind_sensor_start(void);

/* Obtain the most recent measurement from the sensor. */
esp_err_t calypso_wind_sensor_get_reading(calypso_wind_sensor_reading_t *reading);

#ifdef __cplusplus
}
#endif
