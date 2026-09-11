#include "init_mqtt.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "device_config.h"
#include "sensor_config.h"
#include "mqtt_event_handler.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "json_generator.h"
#include "json_utils.h"
#include "mqtt_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "init_mqtt";

static esp_mqtt_client_handle_t mqtt_client;

/* Colon-separated MAC for the JSON payload, e.g. "aa:bb:cc:dd:ee:ff". */
static void format_own_mac_address(char *buffer, size_t buffer_size)
{
    uint8_t mac_address[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_address, ESP_MAC_ETH));

    snprintf(buffer, buffer_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_address[0], mac_address[1], mac_address[2],
             mac_address[3], mac_address[4], mac_address[5]);
}

/* Control channels are namespaced under "control/"; the persisted setting is
 * only the device name. */
static bool format_control_topic(char *buffer, size_t buffer_size)
{
    const char *device_name = device_config_get()->name;
    const int length = snprintf(buffer, buffer_size, "control/%s", device_name);
    return device_name[0] != '\0' && length >= 0 && (size_t)length < buffer_size;
}

void mqtt_publish_braindump(void)
{
    if (mqtt_client == NULL || !mqtt_event_handler_is_connected())
    {
        ESP_LOGW(TAG, "Cannot publish braindump while MQTT is disconnected");
        return;
    }

    const device_config_t *device_config = device_config_get();
    const char *device_name = device_config->name;
    if (device_name[0] == '\0')
    {
        ESP_LOGW(TAG, "Cannot publish braindump without a device name");
        return;
    }

    char reply_topic[sizeof("control_reply/") + DEVICE_CONFIG_TOPIC_SIZE];
    const int topic_length = snprintf(reply_topic, sizeof(reply_topic),
                                      "control_reply/%s", device_name);
    if (topic_length < 0 || (size_t)topic_length >= sizeof(reply_topic))
    {
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
    const char *running_partition_label = running_partition != NULL ? running_partition->label : "";
    const unsigned long running_partition_size = running_partition != NULL ? (unsigned long)running_partition->size : 0;

    char features_json[256];
    if (sensor_json_dump(features_json, sizeof(features_json)) == 0U)
    {
        ESP_LOGE(TAG, "Braindump feature state is too large");
        return;
    }

    char control_topic[DEVICE_CONFIG_TOPIC_SIZE];
    if (!format_control_topic(control_topic, sizeof(control_topic)))
    {
        ESP_LOGE(TAG, "Could not format braindump control topic");
        return;
    }

    char state_json[1024];
    json_gen_str_t generator;
    json_gen_str_start(&generator, state_json, sizeof(state_json), NULL, NULL);
    if (json_gen_start_object(&generator) != 0 ||
        !json_obj_set_escaped_string(&generator, "now", timestamp) ||
        !json_obj_set_escaped_string(&generator, "mac", mac_address_string) ||
        !json_obj_set_escaped_string(&generator, "app_version", esp_app_get_description()->version) ||
        !json_obj_set_escaped_string(&generator, "running_partition", running_partition_label) ||
        json_gen_obj_set_int64(&generator, "running_partition_size", running_partition_size) != 0 ||
        json_gen_obj_set_bool(&generator, "mqtt_connected", true) != 0 ||
        json_gen_obj_set_bool(&generator, "configured", mqtt_event_handler_is_configured()) != 0 ||
        json_gen_push_object(&generator, "device") != 0 ||
        !json_obj_set_escaped_string(&generator, "control_topic", control_topic) ||
        json_gen_pop_object(&generator) != 0 ||
        json_gen_push_array_str(&generator, "features", features_json) != 0 ||
        json_gen_end_object(&generator) != 0)
    {
        ESP_LOGE(TAG, "Braindump state is too large");
        return;
    }

    const int state_length = json_gen_str_end(&generator);
    if (state_length <= 1 || (size_t)state_length > sizeof(state_json))
    {
        ESP_LOGE(TAG, "Braindump state is too large");
        return;
    }

    if (esp_mqtt_client_publish(mqtt_client, reply_topic, state_json, state_length - 1, 1, 0) < 0)
    {
        ESP_LOGW(TAG, "Failed to publish braindump to '%s'", reply_topic);
    }
}

bool mqtt_publish_sensor_reading(const char *topic, float angle_degrees)
{
    if (mqtt_client == NULL || !mqtt_event_handler_is_connected())
    {
        return false;
    }

    char mac_address_string[18];
    format_own_mac_address(mac_address_string, sizeof(mac_address_string));

    time_t current_time = time(NULL);
    struct tm utc_time = {0};
    gmtime_r(&current_time, &utc_time);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc_time);

    char reading_json[128];
    json_gen_str_t generator;
    json_gen_str_start(&generator, reading_json, sizeof(reading_json), NULL, NULL);
    if (json_gen_start_object(&generator) != 0 ||
        !json_obj_set_escaped_string(&generator, "now", timestamp) ||
        json_gen_obj_set_float(&generator, "angle", angle_degrees) != 0 ||
        json_gen_end_object(&generator) != 0 ||
        json_gen_str_end(&generator) <= 1)
    {
        ESP_LOGW(TAG, "Sensor reading JSON is too large");
        return false;
    }

    /* Enqueue rather than publish: this runs on the fixed-rate sensor task, so
     * it must not block waiting for the broker to acknowledge. */
    const int message_id = esp_mqtt_client_enqueue(mqtt_client, topic, reading_json,
                                                   0, 0, true, true);
    if (message_id < 0)
    {
        ESP_LOGW(TAG, "Failed to enqueue a reading for '%s'", topic);
        return false;
    }

    return true;
}

/* The MQTT broker lives on the gateway handed out by DHCP, so the client can
 * only be started once a lease has been acquired. */
static void got_ip_event_handler(void *handler_args, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = event_data;

    if (mqtt_client != NULL)
    {
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
    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialise MQTT client");
        return;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

esp_err_t init_mqtt(void)
{
    return esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                      got_ip_event_handler, NULL);
}
