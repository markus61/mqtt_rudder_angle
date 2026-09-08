#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NVS location of the raw configuration document. The whole broker payload is
 * stored verbatim under one key, so a reboot without a broker can replay it
 * through the same parser that handled it the first time. */
#define CONFIGURATION_NVS_NAMESPACE "config"
#define CONFIGURATION_NVS_PAYLOAD_KEY "payload"

/* Longest MQTT topic accepted from the configuration document. */
#define DEVICE_CONFIG_TOPIC_SIZE 64

/**
 * @brief The device settings that outlive a configuration document.
 *
 * Plain data, so a struct rather than an object with behaviour. Every member
 * has a usable default, which means the device is functional as soon as a
 * sensor pin is known.
 */
typedef struct
{
    /* GPIO the angle sensor is wired to, or -1 while still unknown. */
    int sensor_gpio_number;
    /* Voltage span the sensor produces, mapped onto the degree span below. */
    int sensor_minimum_millivolts;
    int sensor_maximum_millivolts;
    float sensor_minimum_degrees;
    float sensor_maximum_degrees;
    /* How far the angle must move before it is worth another publish. */
    float publish_deadband_degrees;
    char sensor_topic[DEVICE_CONFIG_TOPIC_SIZE];
} device_config_t;

/**
 * @brief Borrow the current settings.
 *
 * The returned pointer stays valid for the lifetime of the program; the
 * contents change when a new configuration document is applied.
 */
const device_config_t *device_config_get(void);

/**
 * @brief Overlay a parsed configuration document onto the current settings.
 *
 * Only members actually present in the document are touched, so a document
 * carrying just a clock update leaves the sensor settings alone. Values that
 * fail validation are logged and skipped, keeping the previous setting.
 *
 * @param configuration Parsed JSON object; ignored when NULL.
 */
void device_config_apply_json(const cJSON *configuration);

/**
 * @brief Restore the settings from the configuration document kept in NVS.
 *
 * @return ESP_OK when a stored document was found and applied,
 *         ESP_ERR_NVS_NOT_FOUND on a device that has never been configured,
 *         or another error from the NVS layer.
 */
esp_err_t device_config_load_from_nvs(void);

#ifdef __cplusplus
}
#endif
