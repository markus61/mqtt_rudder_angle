#include "mqtt_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#include "device_config.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "json_generator.h"
#include "json_utils.h"
#include "mqtt_client.h"
#include "mqtt_event_handler.h"
#include "sensor_config.h"

static const char *TAG = "mqtt_service";

static esp_mqtt_client_handle_t mqtt_client;

/* Telemetry is state, not an event stream: while the network is busy retain
 * only the newest value.  Keeping this queue at one item prevents a slow
 * link from replaying historical readings after it recovers. */
#define MQTT_READING_TOPIC_SIZE 64
#define MQTT_READING_JSON_SIZE 128
#define MQTT_READING_QUEUE_LENGTH 1

typedef struct {
  char topic[MQTT_READING_TOPIC_SIZE];
  char payload[MQTT_READING_JSON_SIZE];
} mqtt_reading_t;

static QueueHandle_t mqtt_reading_queue;

/* QoS 0 publishes are sent immediately by ESP-MQTT and may block on network
 * I/O.  Run them away from the fixed-rate sensor tasks; xQueueOverwrite()
 * ensures that a blocked publisher can have at most one current reading to
 * send when it resumes. */
static void mqtt_reading_publisher_task(void *task_argument) {
  mqtt_reading_t reading;

  while (true) {
    xQueueReceive(mqtt_reading_queue, &reading, portMAX_DELAY);

    if (mqtt_client == NULL || !mqtt_event_handler_is_connected()) {
      continue;
    }

    if (esp_mqtt_client_publish(mqtt_client, reading.topic, reading.payload, 0,
                                0, true) < 0) {
      ESP_LOGW(TAG, "Failed to publish latest reading for '%s'", reading.topic);
    }
  }
}

/* Colon-separated MAC for the JSON payload, e.g. "aa:bb:cc:dd:ee:ff". */
static void format_own_mac_address(char *buffer, size_t buffer_size) {
  uint8_t mac_address[6];
  ESP_ERROR_CHECK(esp_read_mac(mac_address, ESP_MAC_ETH));

  snprintf(buffer, buffer_size, "%02x:%02x:%02x:%02x:%02x:%02x", mac_address[0],
           mac_address[1], mac_address[2], mac_address[3], mac_address[4],
           mac_address[5]);
}

void mqtt_publish_device_braindump(void) {
  if (mqtt_client == NULL || !mqtt_event_handler_is_connected()) {
    ESP_LOGW(TAG, "Cannot publish braindump while MQTT is disconnected");
    return;
  }

  const device_config_t *device_config = device_config_get();
  const char *device_name = device_config->name;
  if (device_name[0] == '\0') {
    ESP_LOGW(TAG, "Cannot publish braindump without a device name");
    return;
  }

  char reply_topic[sizeof("control_reply/") + DEVICE_CONFIG_TOPIC_SIZE];
  const int topic_length = snprintf(reply_topic, sizeof(reply_topic),
                                    "control_reply/%s", device_name);
  if (topic_length < 0 || (size_t)topic_length >= sizeof(reply_topic)) {
    ESP_LOGE(TAG, "Braindump reply topic is too long");
    return;
  }

  char mac_address_string[18];
  format_own_mac_address(mac_address_string, sizeof(mac_address_string));

  time_t current_time = time(NULL);
  struct tm utc_time = {0};
  gmtime_r(&current_time, &utc_time);

  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc_time);

  const esp_partition_t *running_partition = esp_ota_get_running_partition();
  const char *running_partition_label =
      running_partition != NULL ? running_partition->label : "";
  const unsigned long running_partition_size =
      running_partition != NULL ? (unsigned long)running_partition->size : 0;

  char control_topic_prefix[sizeof("control/") + DEVICE_CONFIG_NAME_SIZE +
                            sizeof("/")];
  const int control_topic_prefix_length =
      snprintf(control_topic_prefix, sizeof(control_topic_prefix),
               "control/%s/", device_name);
  if (control_topic_prefix_length < 0 ||
      (size_t)control_topic_prefix_length >= sizeof(control_topic_prefix)) {
    ESP_LOGE(TAG, "Could not format provider control topic prefix");
    return;
  }

  /* Ten instances with maximum-length names/topics exceed the old singleton-
   * sized buffer. Static storage avoids consuming the MQTT event-task stack. */
  static char state_json[4096];
  json_gen_str_t generator;
  json_gen_str_start(&generator, state_json, sizeof(state_json), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      json_gen_push_object(&generator, "configuration") != 0 ||
      !json_obj_set_escaped_string(&generator, "control_topic_prefix",
                                   control_topic_prefix) ||
      json_gen_pop_object(&generator) != 0 ||
      json_gen_push_object(&generator, "runtime") != 0 ||
      !json_obj_set_escaped_string(&generator, "now", timestamp) ||
      !json_obj_set_escaped_string(&generator, "mac", mac_address_string) ||
      !json_obj_set_escaped_string(&generator, "app_version",
                                   esp_app_get_description()->version) ||
      !json_obj_set_escaped_string(&generator, "running_partition",
                                   running_partition_label) ||
      json_gen_obj_set_int64(&generator, "running_partition_size",
                             running_partition_size) != 0 ||
      json_gen_obj_set_bool(&generator, "mqtt_connected", true) != 0 ||
      json_gen_obj_set_bool(&generator, "configured",
                            mqtt_event_handler_is_configured()) != 0 ||
      json_gen_push_array(&generator, "active_sensors") != 0 ||
      !sensor_active_providers_json_add(&generator) ||
      json_gen_pop_array(&generator) != 0 ||
      json_gen_push_array(&generator, "inactive_sensors") != 0 ||
      !sensor_inactive_providers_json_add(&generator) ||
      json_gen_pop_array(&generator) != 0 ||
      json_gen_pop_object(&generator) != 0 ||
      json_gen_end_object(&generator) != 0) {
    ESP_LOGE(TAG, "Braindump state is too large");
    return;
  }

  const int state_length = json_gen_str_end(&generator);
  if (state_length <= 1 || (size_t)state_length > sizeof(state_json)) {
    ESP_LOGE(TAG, "Braindump state is too large");
    return;
  }

  if (esp_mqtt_client_publish(mqtt_client, reply_topic, state_json,
                              state_length - 1, 1, 0) < 0) {
    ESP_LOGW(TAG, "Failed to publish braindump to '%s'", reply_topic);
  }
}

void mqtt_publish_device_nvs_write_result(bool success) {
  if (mqtt_client == NULL || !mqtt_event_handler_is_connected()) {
    ESP_LOGW(TAG, "Cannot publish NVS write result while MQTT is disconnected");
    return;
  }
  const char *device_name = device_config_get()->name;
  if (device_name[0] == '\0') {
    ESP_LOGW(TAG, "Cannot publish NVS write result without a device name");
    return;
  }
  char reply_topic[sizeof("control_reply/") + DEVICE_CONFIG_TOPIC_SIZE];
  const int topic_length = snprintf(reply_topic, sizeof(reply_topic),
                                    "control_reply/%s", device_name);
  if (topic_length < 0 || (size_t)topic_length >= sizeof(reply_topic)) {
    ESP_LOGE(TAG, "NVS write reply topic is too long");
    return;
  }

  time_t current_time = time(NULL);
  struct tm utc_time = {0};
  gmtime_r(&current_time, &utc_time);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc_time);

  char reply_json[96];
  json_gen_str_t generator;
  json_gen_str_start(&generator, reply_json, sizeof(reply_json), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "now", timestamp) ||
      !json_obj_set_escaped_string(&generator, "nvs_write",
                                   success ? "OK" : "ERROR") ||
      json_gen_end_object(&generator) != 0) {
    ESP_LOGE(TAG, "NVS write result is too large");
    return;
  }
  const int reply_length = json_gen_str_end(&generator);
  if (reply_length <= 1 || (size_t)reply_length > sizeof(reply_json)) {
    ESP_LOGE(TAG, "NVS write result is too large");
    return;
  }
  if (esp_mqtt_client_publish(mqtt_client, reply_topic, reply_json,
                              reply_length - 1, 1, 0) < 0) {
    ESP_LOGW(TAG, "Failed to publish NVS write result to '%s'", reply_topic);
  }
}

bool mqtt_publish(const char *topic, const char *payload, size_t payload_length,
                  int qos, bool retain) {
  if (mqtt_client == NULL || !mqtt_event_handler_is_connected()) {
    return false;
  }
  if (topic == NULL || topic[0] == '\0' || payload == NULL ||
      payload_length > INT_MAX) {
    return false;
  }
  return esp_mqtt_client_publish(mqtt_client, topic, payload,
                                 (int)payload_length, qos, retain) >= 0;
}

static bool mqtt_publish_sensor(const char *topic_prefix,
                                const char *provider_name,
                                const char *payload, size_t payload_length) {
  char sensor_component[SENSOR_CONFIG_NAME_SIZE];
  if (!sensor_provider_control_component(provider_name, sensor_component,
                                         sizeof(sensor_component))) {
    return false;
  }
  const char *device_name = device_config_get()->name;
  if (device_name[0] == '\0') {
    return false;
  }
  char topic[sizeof("control_reply/") + DEVICE_CONFIG_NAME_SIZE + 1U +
             SENSOR_CONFIG_NAME_SIZE];
  const int topic_length = snprintf(topic, sizeof(topic), "%s%s/%s",
                                    topic_prefix, device_name, sensor_component);
  return topic_length >= 0 && (size_t)topic_length < sizeof(topic) &&
         mqtt_publish(topic, payload, payload_length, 1, false);
}

bool mqtt_publish_device_reply(const char *payload, size_t payload_length) {
  const char *device_name = device_config_get()->name;
  if (device_name[0] == '\0') {
    return false;
  }
  char topic[sizeof("control_reply/") + DEVICE_CONFIG_NAME_SIZE];
  const int topic_length =
      snprintf(topic, sizeof(topic), "control_reply/%s", device_name);
  return topic_length >= 0 && (size_t)topic_length < sizeof(topic) &&
         mqtt_publish(topic, payload, payload_length, 1, false);
}

bool mqtt_publish_sensor_reply(const char *provider_name, const char *payload,
                               size_t payload_length) {
  return mqtt_publish_sensor("control_reply/", provider_name, payload,
                             payload_length);
}

bool mqtt_publish_telemetry(const char *topic, const char *payload,
                            size_t payload_length) {
  if (mqtt_reading_queue == NULL || mqtt_client == NULL ||
      !mqtt_event_handler_is_connected()) {
    return false;
  }

  if (topic == NULL || topic[0] == '\0' || payload == NULL ||
      strlen(topic) >= MQTT_READING_TOPIC_SIZE ||
      payload_length >= MQTT_READING_JSON_SIZE) {
    ESP_LOGW(TAG, "Sensor topic is invalid or too long");
    return false;
  }

  mqtt_reading_t reading = {0};
  strlcpy(reading.topic, topic, sizeof(reading.topic));
  memcpy(reading.payload, payload, payload_length);
  reading.payload[payload_length] = '\0';

  if (xQueueOverwrite(mqtt_reading_queue, &reading) != pdPASS) {
    ESP_LOGW(TAG, "Failed to queue latest reading for '%s'", topic);
    return false;
  }

  return true;
}

/* The MQTT broker lives on the gateway handed out by DHCP, so the client can
 * only be started once a lease has been acquired. */
static void got_ip_event_handler(void *handler_args,
                                 esp_event_base_t event_base, int32_t event_id,
                                 void *event_data) {
  ip_event_got_ip_t *event = event_data;

  if (mqtt_client != NULL) {
    /* Lease renewed or address changed; the existing client keeps
     * reconnecting on its own, so nothing to do here. */
    return;
  }

  char broker_uri[32];
  snprintf(broker_uri, sizeof(broker_uri), "mqtt://" IPSTR,
           IP2STR(&event->ip_info.gw));
  ESP_LOGI(TAG, "Starting MQTT client, broker at %s", broker_uri);

  esp_mqtt_client_config_t mqtt_config = {
      .broker.address.uri = broker_uri,
  };
  mqtt_client = esp_mqtt_client_init(&mqtt_config);
  if (mqtt_client == NULL) {
    ESP_LOGE(TAG, "Failed to initialise MQTT client");
    return;
  }
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                                 mqtt_event_handler, NULL));
  ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

esp_err_t init_mqtt(void) {
  esp_err_t err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                             got_ip_event_handler, NULL);
  if (err != ESP_OK) {
    return err;
  }

  mqtt_reading_queue =
      xQueueCreate(MQTT_READING_QUEUE_LENGTH, sizeof(mqtt_reading_t));
  if (mqtt_reading_queue == NULL) {
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                 got_ip_event_handler);
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreate(mqtt_reading_publisher_task, "mqtt_telemetry", 4096, NULL, 5,
                  NULL) != pdPASS) {
    vQueueDelete(mqtt_reading_queue);
    mqtt_reading_queue = NULL;
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                 got_ip_event_handler);
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}
