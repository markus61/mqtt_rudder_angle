#pragma once

#include <stdbool.h>

#include "esp_event.h"

/** MQTT callback registered by init_mqtt() for all client events. */
void mqtt_event_handler(void *handler_args, esp_event_base_t event_base,
                        int32_t event_id, void *event_data);

/** Whether the MQTT event callback has observed an active connection. */
bool mqtt_event_handler_is_connected(void);

/** Whether the MQTT event callback has applied a configuration document. */
bool mqtt_event_handler_is_configured(void);
