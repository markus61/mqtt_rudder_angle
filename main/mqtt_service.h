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
 * subscriber can tell readings from different devices apart. The call is
 * non-blocking: it replaces any unsent telemetry with this latest value. A
 * dedicated task sends it at QoS 0, where network I/O is permitted to block.
 *
 * @param topic Topic to publish on.
 * @param angle_degrees Reading in degrees.
 * @return true when the reading replaced the pending telemetry value, false
 *         while the client is disconnected or the telemetry task is absent. A
 *         false return means the caller should keep the reading as unpublished.
 */
bool mqtt_publish_sensor_reading(const char *topic, float angle_degrees);

/** Publish elapsed uptime in seconds as a JSON sensor reading. */
bool mqtt_publish_uptime_reading(const char *topic, float uptime_seconds);

/** Publish device-specific state to control_reply/<device_name>. */
void mqtt_publish_device_braindump(void);

/** Publish one provider's configuration to its symmetric control-reply topic. */
void mqtt_publish_provider_braindump(const char *provider_name);

#ifdef __cplusplus
}
#endif
