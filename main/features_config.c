#include "features_config.h"

#include <stdint.h>

#include "angle_sensor_config.h"
#include "device_config.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "features_config";

/* Tags make an incompatible feature order visible rather than applying a
 * record to the wrong driver. Add a union member and an array entry for each
 * newly attached feature. */
typedef enum
{
    FEATURE_CONFIG_ANGLE_SENSOR = 1,
} feature_config_type_t;

typedef struct
{
    uint32_t type;
    union
    {
        angle_sensor_config_t angle_sensor;
    } settings;
} feature_config_record_t;

#define ATTACHED_FEATURE_COUNT 1U

static void collect_feature_configurations(feature_config_record_t records[ATTACHED_FEATURE_COUNT])
{
    records[0].type = FEATURE_CONFIG_ANGLE_SENSOR;
    records[0].settings.angle_sensor = *angle_sensor_config_get();
}

static esp_err_t apply_feature_configurations(
    const feature_config_record_t records[ATTACHED_FEATURE_COUNT])
{
    if (records[0].type != FEATURE_CONFIG_ANGLE_SENSOR)
    {
        ESP_LOGE(TAG, "Stored feature 0 has type %lu, expected angle sensor",
                 (unsigned long)records[0].type);
        return ESP_ERR_INVALID_STATE;
    }

    angle_sensor_config_restore(&records[0].settings.angle_sensor);
    return ESP_OK;
}

esp_err_t features_config_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "No stored feature configuration: %s", esp_err_to_name(err));
        return err;
    }

    feature_config_record_t records[ATTACHED_FEATURE_COUNT];
    size_t records_size = sizeof(records);
    err = nvs_get_blob(nvs_handle, FEATURES_CONFIG_NVS_KEY, records, &records_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "No stored feature configuration: %s", esp_err_to_name(err));
        return err;
    }
    if (records_size != sizeof(records))
    {
        ESP_LOGE(TAG, "Stored feature configuration has unexpected size %u",
                 (unsigned)records_size);
        return ESP_ERR_INVALID_SIZE;
    }

    err = apply_feature_configurations(records);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Restored %u feature configuration record(s) from NVS",
                 ATTACHED_FEATURE_COUNT);
    }
    return err;
}

esp_err_t features_config_store_to_nvs(void)
{
    feature_config_record_t records[ATTACHED_FEATURE_COUNT] = {0};
    collect_feature_configurations(records);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open configuration NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, FEATURES_CONFIG_NVS_KEY, records, sizeof(records));
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to store feature configuration: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "Stored %u feature configuration record(s) to NVS",
                 ATTACHED_FEATURE_COUNT);
    }
    return err;
}
