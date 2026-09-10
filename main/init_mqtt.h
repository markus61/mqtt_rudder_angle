#pragma once

#include <stdbool.h>

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

/**
 * @brief Publish one sensor reading as a JSON document.
 *
 * The payload carries the current time, the device MAC and the angle, so a
 * subscriber can tell readings from different devices apart. Sent at QoS 0
 * without retain and without blocking, which suits a continuous stream of
 * telemetry where the next reading is only milliseconds away.
 *
 * @param topic Topic to publish on.
 * @param angle_degrees Reading in degrees.
 * @return true when the reading was handed to the MQTT client, false while the
 *         client is not connected or its outbox is full. A false return means
 *         the caller should keep the reading as unpublished.
 */
bool mqtt_publish_sensor_reading(const char *topic, float angle_degrees);

/** Publish the current device state in response to a braindump action. */
void mqtt_publish_braindump(void);

#ifdef __cplusplus
}
#endif
