#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Start sampling the angle sensor at 10 Hz.
     *
     * Reads the pin, sampling configuration and output topic from the angle-sensor
     * configuration, so a configuration document must have supplied a sensor pin
     * first. Each reading is converted to degrees and remembered; a reading that
     * differs from the last published one by the fixed deadband is published to
     * the configured topic.
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
     * @brief Return this angle sensor's braindump feature object as JSON.
     *
     * The returned string is owned by the sensor module and remains valid
     * until the next call to this function.
     *
     * @return A JSON object, or NULL if it could not be formatted.
     */
    const char *angle_sensor_config_dump_json(void);

    /** Return the opaque configuration record persisted for this sensor. */
    const void *angle_sensor_config_get(void);

    /** Return the size of the sensor's opaque persistent configuration record. */
    size_t angle_sensor_config_size(void);

    /** Validate and apply an angle-sensor configuration action. */
    bool angle_sensor_configure(const cJSON *configuration);

#ifdef __cplusplus
}
#endif
