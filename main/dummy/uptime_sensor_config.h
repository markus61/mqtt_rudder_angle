#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#define UPTIME_SENSOR_CONFIG_TOPIC_SIZE 64
#define UPTIME_SENSOR_TYPE_SIZE 32

typedef struct {
  int interval_seconds;
  char sensor_topic[UPTIME_SENSOR_CONFIG_TOPIC_SIZE];
  char sensor_type[UPTIME_SENSOR_TYPE_SIZE];
} uptime_sensor_config_t;

bool uptime_sensor_config_init(uptime_sensor_config_t *configuration,
                               const char *type);
esp_err_t uptime_sensor_config_apply_json(uptime_sensor_config_t *configuration,
                                          const cJSON *json);
