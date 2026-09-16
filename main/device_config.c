/**
 * @file device_config.c
 * @brief Implementation of device configuration management, including loading from and storing to NVS, and validating configuration documents.
 */

#include "device_config.h"
#include "device_utils.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "device_config";

static device_config_t device_config;

/* MQTT control topics embed the device name as one topic level. Restrict it
 * to portable identifier characters so it cannot add a level or act as a
 * subscription wildcard. */
static bool device_name_is_safe_topic_level(const char *name)
{
    if (name == NULL || name[0] == '\0')
    {
        return false;
    }

    for (const unsigned char *character = (const unsigned char *)name;
         *character != '\0'; ++character)
    {
        const bool is_ascii_letter = (*character >= 'A' && *character <= 'Z') ||
                                     (*character >= 'a' && *character <= 'z');
        const bool is_ascii_digit = *character >= '0' && *character <= '9';
        if (!is_ascii_letter && !is_ascii_digit && *character != '_' &&
            *character != '-')
        {
            return false;
        }
    }
    return true;
}

const device_config_t *device_config_get(void)
{
    return &device_config;
}

/**
 * @brief Load the device settings record from NVS. No validation required as validation is performed when record received via MQTT.
 *
 * @return ESP_OK when a stored document was found and applied,
 *         ESP_ERR_NVS_NOT_FOUND on a device that has never been configured,
 *         or another error from the NVS layer.
 */
esp_err_t device_config_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        /* A device that has never been configured has no namespace at all. */
        ESP_LOGI(TAG, "No stored configuration: %s", esp_err_to_name(err));
        return err;
    }

    size_t record_size = sizeof(device_config);
    err = nvs_get_blob(nvs_handle, CONFIGURATION_NVS_DEVICE_KEY,
                       &device_config, &record_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "No stored device configuration: %s", esp_err_to_name(err));
        return err;
    }
    if (record_size != sizeof(device_config))
    {
        ESP_LOGE(TAG, "Stored device configuration has unexpected size %u",
                 (unsigned)record_size);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Restored device configuration from NVS");
    return ESP_OK;
}

/**
 * @brief Store the current device configuration to NVS.
 *
 * @return ESP_OK if the configuration was successfully stored,
 *         or an error code from the NVS layer.
 */
esp_err_t device_config_store_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open configuration NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, CONFIGURATION_NVS_DEVICE_KEY,
                       &device_config, sizeof(device_config));
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to store device configuration: %s", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Validate and store the bare device name from a configuration document.
 *
 * @param configuration The cJSON object containing the configuration.
 * @return true if the device name was successfully validated and stored, false otherwise.
 */
bool device_config_validate(const cJSON *configuration)
{
    if (configuration == NULL)
    {
        return false;
    }

    const cJSON *name_item =
        cJSON_GetObjectItemCaseSensitive(configuration, "your_name");
    if (name_item == NULL)
    {
        /* A configuration document may update only non-persisted settings,
         * such as the clock.  Leave the current device settings untouched. */
        return true;
    }

    const char *name = cJSON_GetStringValue(name_item);

    const size_t max_name_length = DEVICE_CONFIG_NAME_SIZE - 1U;
    if (name != NULL && strlen(name) <= max_name_length &&
        device_name_is_safe_topic_level(name))
    {
        strlcpy(device_config.name, name, sizeof(device_config.name));
        ESP_LOGI(TAG, "Device settings: name='%s'", device_config.name);
        return true;
    }

    ESP_LOGE(TAG,
             "\"your_name\" must be 1-%d ASCII letters, digits, '_' or '-'",
             (int)max_name_length);
    return false;
}

/**
 * @brief Apply a configuration document received from the broker.
 *
 * The document must carry a "now" member holding an RFC3339 timestamp, which
 * is used to set the device clock.
 *
 * @param payload Null-terminated configuration document.
 * @return true when the document is valid and device settings were applied;
 *         false if the payload was rejected.
 */
bool device_configure_from_mqtt(const char *payload)
{
    cJSON *configuration = cJSON_Parse(payload);
    if (configuration == NULL)
    {
        /* cJSON reports the position it choked on, which is more useful than
         * just saying the document was bad. */
        const char *error_position = cJSON_GetErrorPtr();
        if (error_position != NULL)
        {
            ESP_LOGE(TAG, "Configuration is not valid JSON, failed at offset %d: %s",
                     (int)(error_position - payload), error_position);
        }
        else
        {
            ESP_LOGE(TAG, "Configuration is not valid JSON");
        }
        return false;
    }

    if (!cJSON_IsObject(configuration))
    {
        ESP_LOGE(TAG, "Configuration must be a JSON object");
        cJSON_Delete(configuration);
        return false;
    }

    int item_count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, configuration)
    {
        device_utils_log_configuration_item(item);
        item_count++;
    }
    ESP_LOGI(TAG, "Configuration parsed successfully, %d setting(s) received", item_count);

    /* Hand the document to the settings store, which keeps the members it
     * recognises so the rest of the firmware can read them back. */
    if (!device_config_validate(configuration))
    {
        cJSON_Delete(configuration);
        return false;
    }

    /* Time synchronization is useful but is not a prerequisite for accepting
     * and persisting valid device settings. */
    (void)device_utils_clock_set(configuration);
    const char *firmware_url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(configuration, "firmware"));
    if (firmware_url && firmware_url[0] != '\0')
    {
        device_utils_apply_ota_update_request(firmware_url);
    }

    cJSON_Delete(configuration);
    return true;
}
