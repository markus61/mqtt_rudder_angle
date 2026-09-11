#include "mqtt_event_handler.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "device_config.h"
#include "handle_control_action.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

static const char *TAG = "mqtt_event_handler";

#define MQTT_CONFIGURE_REQUEST_TOPIC "config_request"
#define MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX "config/"
#define MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE ((sizeof(MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX) + 12U))

typedef enum
{
    MQTT_TOPIC_UNKNOWN,
    MQTT_TOPIC_CONFIGURE_RESPONSE,
    MQTT_TOPIC_CONTROL,
} mqtt_inbound_topic_t;

static bool device_is_configured;
static volatile bool mqtt_is_connected;

#if SOC_WIFI_SUPPORTED
#define HARDWARE_HAS_WIFI_JSON "true"
#else
#define HARDWARE_HAS_WIFI_JSON "false"
#endif

#if SOC_EMAC_SUPPORTED
#define HARDWARE_HAS_ETHERNET_JSON "true"
#else
#define HARDWARE_HAS_ETHERNET_JSON "false"
#endif

static void format_own_mac_address(char *buffer, size_t buffer_size)
{
    uint8_t mac_address[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_address, ESP_MAC_ETH));
    snprintf(buffer, buffer_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_address[0], mac_address[1], mac_address[2],
             mac_address[3], mac_address[4], mac_address[5]);
}

static void format_config_response_topic(char *buffer, size_t buffer_size)
{
    char mac_address_string[18];
    format_own_mac_address(mac_address_string, sizeof(mac_address_string));

    size_t topic_length = strlen(MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX);
    if (topic_length + 1 > buffer_size)
        topic_length = buffer_size > 0 ? buffer_size - 1U : 0U;

    memcpy(buffer, MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX, topic_length);
    for (const char *character = mac_address_string; *character != '\0'; character++)
    {
        if (*character != ':' && topic_length + 1U < buffer_size)
            buffer[topic_length++] = *character;
    }
    buffer[topic_length] = '\0';
}

static bool format_control_topic(char *buffer, size_t buffer_size)
{
    const char *device_name = device_config_get()->name;
    const int length = snprintf(buffer, buffer_size, "control/%s", device_name);
    return device_name[0] != '\0' && length >= 0 && (size_t)length < buffer_size;
}

static mqtt_inbound_topic_t identify_topic(const char *topic, int topic_length)
{
    char config_response_topic[MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE];
    format_config_response_topic(config_response_topic, sizeof(config_response_topic));
    if ((size_t)topic_length == strlen(config_response_topic) &&
        strncmp(topic, config_response_topic, (size_t)topic_length) == 0)
        return MQTT_TOPIC_CONFIGURE_RESPONSE;

    char control_topic[DEVICE_CONFIG_TOPIC_SIZE];
    if (format_control_topic(control_topic, sizeof(control_topic)) &&
        (size_t)topic_length == strlen(control_topic) &&
        strncmp(topic, control_topic, (size_t)topic_length) == 0)
        return MQTT_TOPIC_CONTROL;

    return MQTT_TOPIC_UNKNOWN;
}

static void subscribe_config_response(esp_mqtt_client_handle_t client)
{
    char response_topic[MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE];
    format_config_response_topic(response_topic, sizeof(response_topic));
    ESP_LOGI(TAG, "Subscribing to configuration response topic '%s'", response_topic);
    esp_mqtt_client_subscribe(client, response_topic, 1);
}

static void subscribe_control_topic(esp_mqtt_client_handle_t client)
{
    char control_topic[DEVICE_CONFIG_TOPIC_SIZE];
    if (!format_control_topic(control_topic, sizeof(control_topic)))
    {
        ESP_LOGI(TAG, "No control topic configured, not subscribing");
        return;
    }
    ESP_LOGI(TAG, "Subscribing to control topic '%s'", control_topic);
    esp_mqtt_client_subscribe(client, control_topic, 1);
}

static void unsubscribe_config_response(esp_mqtt_client_handle_t client)
{
    char response_topic[MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE];
    format_config_response_topic(response_topic, sizeof(response_topic));
    ESP_LOGI(TAG, "Configuration applied, unsubscribing from '%s'", response_topic);
    esp_mqtt_client_unsubscribe(client, response_topic);
}

static void publish_config_request(esp_mqtt_client_handle_t client, const char *mac_address_string)
{
    char request_json[256];
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    const char *running_partition_label = running_partition != NULL ? running_partition->label : "";
    const unsigned long running_partition_size = running_partition != NULL ? (unsigned long)running_partition->size : 0;
    snprintf(request_json, sizeof(request_json),
             "{\"mac\":\"%s\",\"app_version\":\"%s\",\"running_partition\":\"%s\","
             "\"hardware\":{\"chip\":\"%s\",\"running_partition_size\":%lu,"
             "\"has_wifi\":%s,\"has_ethernet\":%s}}",
             mac_address_string, esp_app_get_description()->version, running_partition_label,
             CONFIG_IDF_TARGET, running_partition_size, HARDWARE_HAS_WIFI_JSON,
             HARDWARE_HAS_ETHERNET_JSON);
    esp_mqtt_client_publish(client, MQTT_CONFIGURE_REQUEST_TOPIC, request_json, 0, 1, 0);
}

static void publish_configured_state(esp_mqtt_client_handle_t client)
{
    char mac_address_string[18];
    format_own_mac_address(mac_address_string, sizeof(mac_address_string));

    time_t current_time = time(NULL);
    struct tm utc_time = {0};
    gmtime_r(&current_time, &utc_time);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc_time);

    char state_json[128];
    snprintf(state_json, sizeof(state_json),
             "{\"now\":\"%s\",\"mac\":\"%s\",\"state\":\"configured\"}",
             timestamp, mac_address_string);
    char config_topic[MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE];
    format_config_response_topic(config_topic, sizeof(config_topic));
    esp_mqtt_client_publish(client, config_topic, state_json, 0, 1, 0);
}

bool mqtt_event_handler_is_connected(void)
{
    return mqtt_is_connected;
}

bool mqtt_event_handler_is_configured(void)
{
    return device_is_configured;
}

void mqtt_event_handler(void *handler_args, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        mqtt_is_connected = true;
        subscribe_control_topic(event->client);
        if (device_is_configured)
        {
            ESP_LOGI(TAG, "MQTT connected, configuration already applied");
            break;
        }
        char mac_address_string[18];
        format_own_mac_address(mac_address_string, sizeof(mac_address_string));
        subscribe_config_response(event->client);
        ESP_LOGI(TAG, "MQTT connected, sending configuration request to '%s'",
                 MQTT_CONFIGURE_REQUEST_TOPIC);
        publish_config_request(event->client, mac_address_string);
        break;
    }
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscription confirmed (msg_id=%d)", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "Configuration request delivered (msg_id=%d)", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
    {
        ESP_LOGI(TAG, "Received %.*s: %.*s", event->topic_len, event->topic,
                 event->data_len, event->data);
        switch (identify_topic(event->topic, event->topic_len))
        {
        case MQTT_TOPIC_CONFIGURE_RESPONSE:
        {
            char payload[256];
            int payload_length = event->data_len;
            if (payload_length > (int)sizeof(payload) - 1)
            {
                ESP_LOGW(TAG, "Configuration payload truncated (%d bytes)", event->data_len);
                payload_length = (int)sizeof(payload) - 1;
            }
            memcpy(payload, event->data, (size_t)payload_length);
            payload[payload_length] = '\0';
            if (device_configure_from_mqtt(payload) &&
                device_config_store_to_nvs() == ESP_OK)
            {
                device_is_configured = true;
                unsubscribe_config_response(event->client);
                subscribe_control_topic(event->client);
                publish_configured_state(event->client);
            }
            break;
        }
        case MQTT_TOPIC_CONTROL:
            handle_control_action(event->data, event->data_len);
            break;
        default:
            ESP_LOGW(TAG, "No handler for topic %.*s", event->topic_len, event->topic);
            break;
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        mqtt_is_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected, client will retry automatically");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}
