#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"
#include "json_generator.h"

void *uptime_sensor_create(const char *type);
void uptime_sensor_destroy(void *instance);
const void *uptime_sensor_config_get(const void *instance);
size_t uptime_sensor_config_size(void);
bool uptime_sensor_can_serve_type(const char *type);
size_t uptime_sensor_supported_type_count(void);
const char *uptime_sensor_supported_type(size_t index);
esp_err_t uptime_sensor_configure(void *instance, const cJSON *configuration);
esp_err_t uptime_sensor_config_restore(void *instance, const void *configuration);
esp_err_t uptime_sensor_start(void *instance);
esp_err_t uptime_sensor_config_to_json(const void *instance, json_gen_str_t *json);
esp_err_t uptime_sensor_working_topics_json_add(const void *instance,
                                                json_gen_str_t *json);
esp_err_t uptime_sensor_control_action(void *instance, const char *instance_name,
                                       const char *payload, int payload_length);
