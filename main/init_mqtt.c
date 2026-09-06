#include "init_mqtt.h"

#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"

static const char *TAG = "init_mqtt";

#define MQTT_CONFIGURE_TOPIC "configure"

static esp_mqtt_client_handle_t mqtt_client;

static void mqtt_event_handler(void *handler_args, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected, subscribing to '%s'", MQTT_CONFIGURE_TOPIC);
        esp_mqtt_client_subscribe(event->client, MQTT_CONFIGURE_TOPIC, 1);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscribed to '%s' (msg_id=%d)", MQTT_CONFIGURE_TOPIC, event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Received %.*s: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected, client will retry automatically");
        break;
    case MQTT_EVENT_ERROR:
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
