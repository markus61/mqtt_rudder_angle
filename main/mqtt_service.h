#pragma once

#include <stdbool.h>
#include <stddef.h>

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
 * @brief Queue an already-encoded telemetry document for publication.
 *
 * The call is non-blocking: it replaces any unsent telemetry with this latest
 * value. A dedicated task sends it at QoS 0, where network I/O is permitted
 * to block. Providers own their telemetry JSON schema.
 *
 * @param topic Topic to publish on.
 * @param payload Complete JSON payload.
 * @param payload_length Payload length, excluding its terminating null byte.
 * @return true when the reading replaced the pending telemetry value, false
 *         while the client is disconnected or the telemetry task is absent. A
 *         false return means the caller should keep the reading as unpublished.
 */
bool mqtt_publish_telemetry(const char *topic, const char *payload,
                            size_t payload_length);

/** Publish an opaque payload immediately. MQTT owns no payload schemas. */
bool mqtt_publish(const char *topic, const char *payload, size_t payload_length,
                  int qos, bool retain);

/** Publish an opaque payload on the device's control-reply topic. */
bool mqtt_publish_device_reply(const char *payload, size_t payload_length);

/** Publish an opaque provider payload on its control-reply topic. */
bool mqtt_publish_provider_reply(const char *provider_name, const char *payload,
                                 size_t payload_length);

/** Publish an opaque provider payload on its control topic. */
bool mqtt_publish_provider_control(const char *provider_name,
                                   const char *payload,
                                   size_t payload_length);

/** Publish device-specific state to control_reply/<device_name>. */
void mqtt_publish_device_braindump(void);

/** Publish the result of a device-configuration NVS write. */
void mqtt_publish_device_nvs_write_result(bool success);

#ifdef __cplusplus
}
#endif
