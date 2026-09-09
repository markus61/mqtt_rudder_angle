#include "angle_sensor.h"
#include "features_config.h"
#include "device_config.h"
#include "mant1s_ethernet.h"
#include "init_mqtt.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "main";

static void initialise_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void confirm_ota_image_after_startup(void)
{
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    esp_err_t err = esp_ota_get_state_partition(running_partition, &ota_state);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to read OTA image state: %s", esp_err_to_name(err));
        return;
    }

    if (ota_state != ESP_OTA_IMG_PENDING_VERIFY)
    {
        return;
    }

    ESP_LOGI(TAG, "OTA image is pending verification; marking startup as valid");
    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mark OTA image valid: %s", esp_err_to_name(err));
    }
#endif
}

void app_main(void)
{
    initialise_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "ManT1S 10BASE-T1S bring-up");

    /* Restore separate device and feature records. A device that has never
     * been configured simply has nothing stored yet. */
    device_config_load_from_nvs();
    features_config_load_from_nvs();

    /* MQTT is handled in init_mqtt.c: once DHCP delivers a lease, a client
     * connects to the broker on the gateway and publishes a configuration
     * request to "config_request". */
    ESP_ERROR_CHECK(init_mqtt());

    ESP_ERROR_CHECK(mant1s_ethernet_start(NULL));

    confirm_ota_image_after_startup();

    /* Start sampling the angle sensor. This is the device's steady-state job:
     * read the sensor at 10 Hz, convert to degrees and publish whenever the
     * angle changes. Without a stored sensor pin it declines to start, and
     * init_mqtt.c starts it as soon as a configuration document supplies one. */
    /* rely on the task started after mqtt configuration
    angle_sensor_start(); */

    /* Link and DHCP progress is reported by the event handlers in
     * mant1s_ethernet.c, and the angle sensor runs on its own task, so there
     * is nothing to poll here. */
}
