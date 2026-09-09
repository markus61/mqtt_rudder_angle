#include "init_mqtt.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "angle_sensor.h"
#include "cJSON.h"
#include "configuration.h"
#include "device_config.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

static const char *TAG = "init_mqtt";

#define MQTT_CONFIGURE_REQUEST_TOPIC "config_request"
/* Topic the broker answers on. MQTT topic levels are separated by '/', so the
 * MAC is used without its colons to keep the topic a single level. */
#define MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX "config/"
/* Prefix plus 12 MAC digits plus terminator. Wrap the expression so it is
 * always treated as a single compile-time size expression. */
#define MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE ((sizeof(MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX) + 12U))

/* Known inbound topics. C switch() needs an integer, so incoming topic strings
 * are mapped onto this enum first. */
typedef enum
{
    MQTT_TOPIC_UNKNOWN,
    MQTT_TOPIC_CONFIGURE_RESPONSE,
    MQTT_TOPIC_CONTROL,
} mqtt_inbound_topic_t;

static esp_mqtt_client_handle_t mqtt_client;
/* Set once a configuration has been applied. Only touched from the MQTT event
 * handler, which runs on a single task, so no locking is needed. */
static bool device_is_configured;
/* Whether the client is currently usable for publishing. Written from the MQTT
 * event handler and read by the sensor task; a plain bool is enough, because a
 * reading lost to a stale value is simply retried on the next sample. */
static volatile bool mqtt_is_connected;

/* CONFIGURATION_NVS_NAMESPACE and CONFIGURATION_NVS_PAYLOAD_KEY come from
 * device_config.h, which also reads the document back on the next boot. */

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

/* Colon-separated MAC for the JSON payload, e.g. "aa:bb:cc:dd:ee:ff". */
static void format_own_mac_address(char *buffer, size_t buffer_size)
{
    uint8_t mac_address[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_address, ESP_MAC_ETH));

    snprintf(buffer, buffer_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_address[0], mac_address[1], mac_address[2],
             mac_address[3], mac_address[4], mac_address[5]);
}

/* Builds "config/<own MAC without colons>", e.g. "config/aabbccddeeff".
   uses the result to subscribe and unsubscribe from the MQTT broker.
   a managing application can use this topic to send configuration responses. */
static void format_config_response_topic(char *buffer, size_t buffer_size)
{
    char mac_address_string[18];
    format_own_mac_address(mac_address_string, sizeof(mac_address_string));

    size_t topic_length = strlen(MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX);
    if (topic_length + 1 > buffer_size)
    {
        topic_length = buffer_size > 0 ? buffer_size - 1U : 0U;
    }

    memcpy(buffer, MQTT_CONFIGURE_RESPONSE_TOPIC_PREFIX, topic_length);
    for (const char *character = mac_address_string; *character != '\0'; character++)
    {
        if (*character != ':' && topic_length + 1U < buffer_size)
        {
            buffer[topic_length++] = *character;
        }
    }
    buffer[topic_length] = '\0';
}

/* Maps an incoming topic, which is not null terminated, onto the topic enum. */
static mqtt_inbound_topic_t identify_topic(const char *topic, int topic_length)
{
    char config_response_topic[MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE];
    format_config_response_topic(config_response_topic, sizeof(config_response_topic));

    if ((size_t)topic_length == strlen(config_response_topic) &&
        strncmp(topic, config_response_topic, (size_t)topic_length) == 0)
    {
        return MQTT_TOPIC_CONFIGURE_RESPONSE;
    }

    const char *control_topic = device_config_get()->control_topic;
    if (control_topic[0] != '\0' &&
        (size_t)topic_length == strlen(control_topic) &&
        strncmp(topic, control_topic, (size_t)topic_length) == 0)
    {
        return MQTT_TOPIC_CONTROL;
    }

    return MQTT_TOPIC_UNKNOWN;
}

typedef enum
{
    CMD_UNKNOWN = -1,
    CMD_CALIBRATE,
    CMD_BRAINDUMP
} control_action;

control_action parse_action(const char *action_string)
{
    if (strcasecmp(action_string, "calibrate") == 0)
        return CMD_CALIBRATE;
    if (strcasecmp(action_string, "braindump") == 0)
        return CMD_BRAINDUMP;
    return CMD_UNKNOWN;
}

static void device_control_braindump()
{
    if (mqtt_client == NULL || !mqtt_is_connected)
    {
        ESP_LOGW(TAG, "Cannot publish braindump while MQTT is disconnected");
        return;
    }

    const device_config_t *config = device_config_get();
    const char *device_name = strrchr(config->control_topic, '/');
    device_name = device_name != NULL ? device_name + 1 : config->control_topic;
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

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    const char *running_partition_label = running_partition != NULL ? running_partition->label : "";
    const unsigned long running_partition_size = running_partition != NULL ? (unsigned long)running_partition->size : 0;

    /** Dump the device's internal state as a JSON object */
    char state_json[1024];
    const int state_length = snprintf(
        state_json, sizeof(state_json),
        "{\"mac\":\"%s\",\"app_version\":\"%s\",\"running_partition\":\"%s\","
        "\"running_partition_size\":%lu,\"mqtt_connected\":true,\"configured\":%s,"
        "\"sensor_gpio\":%d,\"sensor_min_mv\":%d,\"sensor_max_mv\":%d,"
        "\"sensor_min_deg\":%.2f,\"sensor_max_deg\":%.2f,"
        "\"publish_deadband_deg\":%.2f,\"sensor_samples_per_reading\":%d,\"sensor_sample_period_ms\":%d,\"sensor_topic\":\"%s\","
        "\"control_topic\":\"%s\"}",
        mac_address_string, esp_app_get_description()->version, running_partition_label,
        running_partition_size, device_is_configured ? "true" : "false",
        config->sensor_gpio_number, config->sensor_minimum_millivolts,
        config->sensor_maximum_millivolts, config->sensor_minimum_degrees,
        config->sensor_maximum_degrees, config->sensor_deadband_degrees,
        config->sensor_samples_per_reading, config->sensor_sample_period_ms,
        config->sensor_topic, config->control_topic);
    if (state_length < 0 || (size_t)state_length >= sizeof(state_json))
    {
        ESP_LOGE(TAG, "Braindump state is too large");
        return;
    }

    if (esp_mqtt_client_publish(mqtt_client, reply_topic, state_json, state_length, 1, 0) < 0)
    {
        ESP_LOGW(TAG, "Failed to publish braindump to '%s'", reply_topic);
    }
}

/* Control messages are JSON objects with a string "name" member. */
static void handle_control_action(const char *payload, int payload_length)
{
    cJSON *action_json = cJSON_ParseWithLength(payload, (size_t)payload_length);
    if (!cJSON_IsObject(action_json))
    {
        ESP_LOGW(TAG, "Control action must be a JSON object");
        cJSON_Delete(action_json);
        return;
    }

    const cJSON *name = cJSON_GetObjectItemCaseSensitive(action_json, "name");
    if (!cJSON_IsString(name) || name->valuestring == NULL)
    {
        ESP_LOGW(TAG, "Control action is missing a string \"name\" member");
        cJSON_Delete(action_json);
        return;
    }

    control_action action = parse_action(name->valuestring);
    switch (action)
    {
    case CMD_CALIBRATE:
        ESP_LOGI(TAG, "Handling control action 'calibrate'");
        angle_sensor_calibrate();
        break;
    case CMD_BRAINDUMP:
        ESP_LOGI(TAG, "Handling control action 'braindump'");
        device_control_braindump();
        break;
    default:
        ESP_LOGW(TAG, "Unknown control action '%s'", name->valuestring);
        break;
    }

    cJSON_Delete(action_json);
}

static void subscribe_config_response(esp_mqtt_client_handle_t client)
{
    char response_topic[MQTT_CONFIGURE_RESPONSE_TOPIC_SIZE];
    format_config_response_topic(response_topic, sizeof(response_topic));

    ESP_LOGI(TAG, "Subscribing to configuration response topic '%s'", response_topic);
    esp_mqtt_client_subscribe(client, response_topic, 1);
}

/* The control topic comes from the configuration document rather than being
 * derived from the MAC, so a device that was never told one simply has no
 * control channel and there is nothing to subscribe to. */
static void subscribe_control_topic(esp_mqtt_client_handle_t client)
{
    const char *control_topic = device_config_get()->control_topic;
    if (control_topic[0] == '\0')
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

static void publish_config_request(esp_mqtt_client_handle_t client,
                                   const char *mac_address_string)
{
    char request_json[256];
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    const char *running_partition_label = running_partition != NULL ? running_partition->label : "";
    unsigned long running_partition_size = running_partition != NULL
                                               ? (unsigned long)running_partition->size
                                               : 0;

    snprintf(request_json, sizeof(request_json),
             "{\"mac\":\"%s\",\"app_version\":\"%s\",\"running_partition\":\"%s\","
             "\"hardware\":{\"chip\":\"%s\",\"running_partition_size\":%lu,"
             "\"has_wifi\":%s,\"has_ethernet\":%s}}",
             mac_address_string, esp_app_get_description()->version,
             running_partition_label, CONFIG_IDF_TARGET, running_partition_size,
             HARDWARE_HAS_WIFI_JSON, HARDWARE_HAS_ETHERNET_JSON);

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

bool mqtt_publish_sensor_reading(const char *topic, float angle_degrees)
{
    if (mqtt_client == NULL || !mqtt_is_connected)
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
    snprintf(reading_json, sizeof(reading_json),
             "{\"now\":\"%s\",\"mac\":\"%s\",\"angle\":%.2f}",
             timestamp, mac_address_string, angle_degrees);

    /* Enqueue rather than publish: this runs on the fixed-rate sensor task, so
     * it must not block waiting for the broker to acknowledge. */
    const int message_id = esp_mqtt_client_enqueue(mqtt_client, topic, reading_json,
                                                   0, 0, 0, true);
    if (message_id < 0)
    {
        ESP_LOGW(TAG, "Failed to enqueue a reading for '%s'", topic);
        return false;
    }

    return true;
}

static bool persist_configuration_data(const char *payload)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open configuration NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(nvs_handle, CONFIGURATION_NVS_PAYLOAD_KEY, payload);
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to persist configuration data: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Configuration data persisted to NVS");
    return true;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        mqtt_is_connected = true;

        /* Subscriptions do not survive a reconnect, so the control topic is
         * taken out again on every connect. This also covers a reboot that
         * restored its settings from NVS, where no configuration document
         * arrives to trigger the subscription. */
        subscribe_control_topic(event->client);

        if (device_is_configured)
        {
            /* Already configured; the broker has nothing left to tell us, so
             * do not ask again. */
            ESP_LOGI(TAG, "MQTT connected, configuration already applied");
            break;
        }

        char mac_address_string[18];
        format_own_mac_address(mac_address_string, sizeof(mac_address_string));

        /* Subscribe before publishing so a fast broker reply is not missed. */
        subscribe_config_response(event->client);

        ESP_LOGI(TAG, "MQTT connected, sending configuration request to '%s'", MQTT_CONFIGURE_REQUEST_TOPIC);
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
        ESP_LOGI(TAG, "Received %.*s: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);

        switch (identify_topic(event->topic, event->topic_len))
        {
        case MQTT_TOPIC_CONFIGURE_RESPONSE:
        {
            /* The payload is not null terminated, so copy it before use. */
            char payload[256];
            int payload_length = event->data_len;
            if (payload_length > (int)sizeof(payload) - 1)
            {
                ESP_LOGW(TAG, "Configuration payload truncated (%d bytes)", event->data_len);
                payload_length = (int)sizeof(payload) - 1;
            }
            memcpy(payload, event->data, (size_t)payload_length);
            payload[payload_length] = '\0';

            if (configure_this_device(payload) && persist_configuration_data(payload))
            {
                /* Configured, so the topic is no longer of interest. The flag
                 * also stops the next reconnect from resubscribing. */
                device_is_configured = true;
                unsubscribe_config_response(event->client);
                subscribe_control_topic(event->client);
                publish_configured_state(event->client);

                /* The document may have supplied the sensor pin, so start
                 * sampling now instead of waiting for the next boot. Doing
                 * nothing when already running is handled inside. */
                angle_sensor_start();
            }
            break;
        }
        case MQTT_TOPIC_CONTROL:
            handle_control_action(event->data, event->data_len);
            break;
        case MQTT_TOPIC_UNKNOWN:
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
        /* ToDo: load local configuration from persistent storage if MQTT fails.*/
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
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
