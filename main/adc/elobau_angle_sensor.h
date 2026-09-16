/**
 * @file elobau_angle_sensor.h
 * @brief Interface for the Elobau angle sensor module.
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

/**
 * @brief Start sampling the angle sensor.
 *
 * Reads the pin, sampling configuration and output topic from the angle-sensor
 * configuration, so a configuration document must have supplied a sensor pin
 * first. Each reading is converted to degrees and remembered; a reading that
 * differs from the last published one by the configured deadband is published to
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

/** Return the opaque configuration record persisted for this sensor. */
const void *angle_sensor_config_get(void);

/** Return this hardware's fixed model type. */
const char *angle_sensor_current_type(void);

/** Return the size of the sensor's opaque persistent configuration record. */
size_t angle_sensor_config_size(void);

/** Return whether this sensor supports the supplied model type. */
bool angle_sensor_can_serve_type(const char *type);

/** Return the number of model types supported by this provider. */
size_t angle_sensor_supported_type_count(void);

/** Return a supported model type by index, or NULL when out of range. */
const char *angle_sensor_supported_type(size_t index);

/**
 * @brief Configure the angle sensor with the given JSON configuration.
 *        Validate and apply an angle-sensor configuration action.
 *
 * @param configuration The cJSON object containing the configuration.
 * @return ESP_OK if the configuration was successfully applied; otherwise a
 *         validation error. The live configuration is unchanged on failure.
 */
esp_err_t angle_sensor_configure(const cJSON *configuration);

/** Restore a trusted record of config_size() bytes; no MQTT validation.
 * Returns ESP_ERR_INVALID_ARG for a null record. */
esp_err_t angle_sensor_config_restore(const void *configuration);

/** Append provider settings (excluding name/type) to an open JSON object. */
esp_err_t angle_sensor_config_to_json(json_gen_str_t *json);

/** Append normal post-start publication topics to an open JSON array. Control
 * replies are common MQTT routing and are deliberately not listed here. */
esp_err_t angle_sensor_working_topics_json_add(json_gen_str_t *json);

/** Handle a control payload received on this provider's MQTT topic.
 * Returns ESP_OK when handled, otherwise an error describing rejection. */
esp_err_t angle_sensor_control_action(const char *payload, int payload_length);

#ifdef __cplusplus
}
#endif
