#pragma once

#include <stddef.h>

#define UPTIME_SENSOR_CONFIG_TOPIC_SIZE 64
#define UPTIME_SENSOR_TYPE_SIZE 32

typedef struct {
  int interval_seconds;
  char sensor_topic[UPTIME_SENSOR_CONFIG_TOPIC_SIZE];
  char sensor_type[UPTIME_SENSOR_TYPE_SIZE];
} uptime_sensor_config_t;

const uptime_sensor_config_t *uptime_sensor_config(void);
