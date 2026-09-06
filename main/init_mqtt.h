#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arrange for the MQTT client to start once the network is up.
 *
 * Registers a handler for IP_EVENT_ETH_GOT_IP. When a DHCP lease arrives,
 * an MQTT client is started against the broker on the gateway address. On
 * every connect it subscribes to "config/<own MAC without colons>" and then
 * publishes a configuration request (device MAC and app version as JSON) to
 * the "config_request" topic. Reconnects are handled by the MQTT client
 * itself.
 *
 * Requires esp_event_loop_create_default() to have been called first.
 *
 * @return ESP_OK on success, otherwise the error from event registration.
 */
esp_err_t init_mqtt(void);

#ifdef __cplusplus
}
#endif
