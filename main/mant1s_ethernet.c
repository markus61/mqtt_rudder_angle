/*
 * 10BASE-T1S bring-up for the Silicognition ManT1S board.
 *
 * Hardware (mirrors the MicroPython SIL_MANT1S board definition):
 *   MCU        ESP32-PICO-V3-02
 *   PHY        Microchip LAN8670, 10BASE-T1S, SMI address 0
 *   MDC        GPIO8    (differs from the ESP32 default of GPIO23)
 *   MDIO       GPIO7    (differs from the ESP32 default of GPIO18)
 *   PHY reset  none, the PHY has no reset line wired to the MCU
 *   RMII clock external input on GPIO0
 */

#include "mant1s_ethernet.h"

#include "esp_check.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy_lan867x.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"

static const char *TAG = "mant1s_eth";

/* SMI (MDC/MDIO) pins as routed on the ManT1S board. */
#define MANT1S_ETH_MDC_GPIO  8
#define MANT1S_ETH_MDIO_GPIO 7

/* The LAN8670 is strapped to SMI address 0. Pinning it explicitly rather than
 * using ESP_ETH_PHY_ADDR_AUTO makes a miswired SMI bus fail loudly instead of
 * silently binding to whatever else answers. */
#define MANT1S_ETH_PHY_ADDR 0

static void on_ethernet_event(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
        uint8_t mac_addr[ETH_ADDR_LEN] = { 0 };
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "T1S link up, MAC " MACSTR, MAC2STR(mac_addr));
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "T1S link down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "driver started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "driver stopped");
        break;
    default:
        break;
    }
}

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "DHCP lease acquired");
    ESP_LOGI(TAG, "  address " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "  netmask " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "  gateway " IPSTR, IP2STR(&ip_info->gw));
}

esp_err_t mant1s_ethernet_start(esp_eth_handle_t *out_eth_handle)
{
    /* --- MAC: ESP32 internal EMAC, RMII, board-specific SMI pins --- */
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_gpio.mdc_num = MANT1S_ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = MANT1S_ETH_MDIO_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    ESP_RETURN_ON_FALSE(mac != NULL, ESP_FAIL, TAG, "failed to create EMAC instance");

    /* --- PHY: LAN8670 10BASE-T1S --- */
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = MANT1S_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = -1;  /* no MCU-driven reset line on this board */

    /* 10BASE-T1S is a single-speed, half-duplex, point-to-multipoint link with
     * no clause-28 auto-negotiation, so the autoneg timeout is meaningless. */
    phy_config.autonego_timeout_ms = 0;

    esp_eth_phy_t *phy = esp_eth_phy_new_lan867x(&phy_config);
    ESP_RETURN_ON_FALSE(phy != NULL, ESP_FAIL, TAG, "failed to create LAN867x PHY instance");

    /* --- Driver --- */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_config, &eth_handle), TAG,
                        "failed to install Ethernet driver");

    /* --- netif glue, with the DHCP client left enabled by default --- */
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_config);
    ESP_RETURN_ON_FALSE(eth_netif != NULL, ESP_FAIL, TAG, "failed to create Ethernet netif");

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    ESP_RETURN_ON_FALSE(glue != NULL, ESP_FAIL, TAG, "failed to create netif glue");
    ESP_RETURN_ON_ERROR(esp_netif_attach(eth_netif, glue), TAG, "failed to attach netif");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                                   on_ethernet_event, NULL),
                        TAG, "failed to register Ethernet event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                                   on_got_ip, NULL),
                        TAG, "failed to register got-IP event handler");

    ESP_RETURN_ON_ERROR(esp_eth_start(eth_handle), TAG, "failed to start Ethernet");

    ESP_LOGI(TAG, "LAN8670 initialised, MDC=GPIO%d MDIO=GPIO%d addr=%d",
             MANT1S_ETH_MDC_GPIO, MANT1S_ETH_MDIO_GPIO, MANT1S_ETH_PHY_ADDR);

    if (out_eth_handle != NULL) {
        *out_eth_handle = eth_handle;
    }
    return ESP_OK;
}
