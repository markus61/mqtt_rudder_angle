#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start sampling the angle sensor at 10 Hz.
 *
 * Reads the pin and the conversion span from the device configuration, so a
 * configuration document must have supplied a sensor pin first. Each reading is
 * converted to degrees and remembered; a reading that differs from the last
 * published one by at least the configured deadband is published to the
 * configured topic.
 *
 * Safe to call more than once: later calls report that sampling is already
 * running and change nothing, so both the boot path and the arrival of a
 * configuration document can call it.
 *
 * @return ESP_OK once the sampling task is running,
 *         ESP_ERR_INVALID_STATE when no sensor pin is configured,
 *         or an error from the ADC driver.
 */
esp_err_t angle_sensor_start(void);

/**
 * @brief Handle a request to calibrate the angle sensor.
 *
 * Calibration is not implemented yet; receiving the action is logged so the
 * control path is observable until the calibration procedure is added.
 */
void angle_sensor_calibrate(void);

#ifdef __cplusplus
}
#endif
