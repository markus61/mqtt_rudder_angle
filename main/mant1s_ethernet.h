#pragma once

#include "esp_err.h"
#include "esp_eth_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the ManT1S 10BASE-T1S interface and start DHCP.
 *
 * Initialises the ESP32 EMAC in RMII mode, attaches the LAN8670 PHY, glues the
 * driver to an esp_netif with the DHCP client enabled, and starts the link.
 * This function does not block until a lease is acquired; watch for the
 * IP_EVENT_ETH_GOT_IP event (already logged by this module) instead.
 *
 * Requires esp_netif_init() and esp_event_loop_create_default() to have been
 * called first.
 *
 * @param[out] out_eth_handle Receives the driver handle, may be NULL.
 * @return ESP_OK on success, otherwise an error from the underlying driver.
 */
esp_err_t mant1s_ethernet_start(esp_eth_handle_t *out_eth_handle);

#ifdef __cplusplus
}
#endif
