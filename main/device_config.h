#pragma once

#include <stdbool.h>

#include "device_config.h"

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Configuration records share one namespace. Keep keys short: NVS keys are
 * limited to 15 characters. */
#define CONFIGURATION_NVS_NAMESPACE "config"
#define CONFIGURATION_NVS_DEVICE_KEY "device_config"

/* Longest MQTT topic accepted from the configuration document. */
#define DEVICE_CONFIG_TOPIC_SIZE 64
#define DEVICE_CONFIG_NAME_SIZE 64

    /**
     * @brief Apply a configuration document received from the broker.
     *
     * The document must carry a "now" member holding an RFC3339 timestamp, which
     * is used to set the device clock.
     *
     * @param payload Null-terminated configuration document.
     * @return true when the document is valid and its device settings have
     *         been applied; false if the payload was rejected.  The optional
     *         "now" member is applied to the clock independently and does not
     *         determine whether device settings are accepted.
     */

    bool device_configure_from_mqtt(const char *payload);

    /**
     * @brief The device settings that outlive a configuration document.
     *
     * Plain data, so a struct rather than an object with behaviour.
     */
    typedef struct
    {
        /* Name of the device. It is one MQTT topic level, restricted to ASCII
         * letters, digits, '_' and '-'. Empty until a configuration document
         * supplies one, because there is no sensible default: a guessed name
         * would either collide with another device or be silently wrong. */
        char name[DEVICE_CONFIG_NAME_SIZE];
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
     * Only members actually present in the document are touched. A supplied
     * value that fails validation rejects the document without changing the
     * current setting.
     *
     * @param configuration Parsed JSON object.
     * @return true when all supplied device settings are valid.
     */
    bool device_config_validate(const cJSON *configuration);

    /**
     * @brief Restore the device settings record from NVS.
     *
     * @return ESP_OK when a stored document was found and applied,
     *         ESP_ERR_NVS_NOT_FOUND on a device that has never been configured,
     *         or another error from the NVS layer.
     */
    esp_err_t device_config_load_from_nvs(void);

    /** Store the device settings record in NVS. */
    esp_err_t device_config_store_to_nvs(void);

#ifdef __cplusplus
}
#endif
