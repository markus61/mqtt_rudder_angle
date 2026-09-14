#include "mqtt_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "device_config.h"

/* A log record should remain bounded: unlike telemetry, an unexpected long
 * diagnostic must not consume an unbounded amount of stack or MQTT memory. */
#define MQTT_LOG_MESSAGE_SIZE 256
#define MQTT_LOG_TOPIC_SIZE (sizeof("log/") + DEVICE_CONFIG_NAME_SIZE)

static esp_mqtt_client_handle_t mqtt_log_client;

void mqtt_log_set_client(esp_mqtt_client_handle_t client) {
  mqtt_log_client = client;
}

void mqtt_log(esp_log_level_t level, const char *tag, const char *format, ...) {
  if (tag == NULL || format == NULL) {
    return;
  }

  char message[MQTT_LOG_MESSAGE_SIZE];
  va_list arguments;
  va_start(arguments, format);
  const int message_length = vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);

  /* Use ESP-LOG for the local output rather than printf so filtering and the
   * selected console transport continue to work exactly as elsewhere. */
  if (message_length < 0) {
    ESP_LOG_LEVEL(level, tag, "%s", "<log formatting failed>");
    return;
  }
  ESP_LOG_LEVEL(level, tag, "%s", message);

  const char *device_name = device_config_get()->name;
  if (mqtt_log_client == NULL || device_name[0] == '\0') {
    return;
  }

  char topic[MQTT_LOG_TOPIC_SIZE];
  const int topic_length = snprintf(topic, sizeof(topic), "log/%s", device_name);
  if (topic_length < 0 || (size_t)topic_length >= sizeof(topic)) {
    return;
  }

  /* vsnprintf returns the would-be size.  Publishing the buffer size minus
   * one makes truncation explicit and always leaves the MQTT payload valid. */
  const int payload_length =
      message_length < (int)sizeof(message) ? message_length : sizeof(message) - 1;
  (void)esp_mqtt_client_publish(mqtt_log_client, topic, message, payload_length,
                                0, 0);
}
