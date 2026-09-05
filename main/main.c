#include "mant1s_ethernet.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static void initialise_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    initialise_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "ManT1S 10BASE-T1S bring-up");
    ESP_ERROR_CHECK(mant1s_ethernet_start(NULL));

    /* Link and DHCP progress is reported by the event handlers in
     * mant1s_ethernet.c, so there is nothing to poll here. */
}
