#include "device_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "device_config";

static device_config_t device_config;

const device_config_t *device_config_get(void)
{
    return &device_config;
}

/* Reads a topic member, leaving the destination untouched when the member is
 * absent, empty or too long. Both topics validate identically, so they share
 * one helper rather than a copy each. */
static void apply_topic_member(const cJSON *configuration, const char *member_name,
                               char *destination, size_t destination_size)
{
    const char *topic = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration, member_name));
    if (topic == NULL)
    {
        return;
    }

    if (topic[0] == '\0')
    {
        ESP_LOGE(TAG, "\"%s\" must not be empty, keeping '%s'", member_name, destination);
        return;
    }
    if (strlen(topic) >= destination_size)
    {
        ESP_LOGE(TAG, "\"%s\" is longer than %d characters, keeping '%s'",
                 member_name, (int)destination_size - 1, destination);
        return;
    }

    strlcpy(destination, topic, destination_size);
}

void device_config_apply_json(const cJSON *configuration)
{
    if (configuration == NULL)
    {
        return;
    }

    apply_topic_member(configuration, "control_topic", device_config.control_topic,
                       sizeof(device_config.control_topic));
    ESP_LOGI(TAG, "Device settings: control topic='%s'", device_config.control_topic);
}

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
