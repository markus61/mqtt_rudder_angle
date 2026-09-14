#pragma once

#include "esp_log.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the client used to mirror log messages to MQTT.
 *
 * The client is borrowed; it must remain valid until it is replaced with
 * mqtt_log_set_client(NULL).  Messages are silently kept local while no
 * client is configured or while the device has no configured name.
 */
void mqtt_log_set_client(esp_mqtt_client_handle_t client);

/**
 * @brief Log a formatted message locally and, when possible, over MQTT.
 *
 * The message is emitted through ESP_LOG_LEVEL(), so ESP-IDF's normal
 * per-tag and global log-level filtering still applies to the console.  The
 * same formatted message is published without retention at QoS 0 to
 * "log/<device_name>".  Publishing is best effort and never produces a log
 * message of its own, avoiding a logging recursion when MQTT is unavailable.
 */
void mqtt_log(esp_log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#define MQTT_LOGE(tag, format, ...) \
  mqtt_log(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define MQTT_LOGW(tag, format, ...) \
  mqtt_log(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define MQTT_LOGI(tag, format, ...) \
  mqtt_log(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define MQTT_LOGD(tag, format, ...) \
  mqtt_log(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define MQTT_LOGV(tag, format, ...) \
  mqtt_log(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
