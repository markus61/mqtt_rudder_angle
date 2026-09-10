#include "device_config.h"
#include "device_utils.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "device_config";

static device_config_t device_config;

const device_config_t *device_config_get(void)
{
    return &device_config;
}

/** Validate and store the bare device name from a configuration document. */
void device_config_validate(const cJSON *configuration)
{
    if (configuration == NULL)
    {
        return;
    }
    const char *name = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration, "your_name"));

    const size_t max_name_length = DEVICE_CONFIG_TOPIC_SIZE - sizeof("control/");
    if (name != NULL && name[0] != '\0' && strlen(name) <= max_name_length)
    {
        strlcpy(device_config.name, name, sizeof(device_config.name));
    }
    else if (name != NULL)
    {
        ESP_LOGE(TAG, "\"your_name\" must be non-empty and at most %d characters, keeping '%s'",
                 (int)max_name_length, device_config.name);
    }
    ESP_LOGI(TAG, "Device settings: name='%s'", device_config.name);
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
 * @brief Apply a configuration document received from the broker.
 *
 * The document must carry a "now" member holding an RFC3339 timestamp, which
 * is used to set the device clock.
 *
 * @param payload Null-terminated configuration document.
 * @return true only if the device clock was set from "now", so the caller can
 *         stop listening for it; false if the payload was rejected.
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
    device_config_validate(configuration);
    const bool clock_was_set = device_utils_clock_set(configuration);
    const char *firmware_url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(configuration, "firmware"));
    if (firmware_url && firmware_url[0] != '\0')
    {
        device_utils_apply_ota_update_request(firmware_url);
    }

    cJSON_Delete(configuration);
    return clock_was_set;
}
