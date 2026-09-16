#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANGLE_SENSOR_CONFIG_TOPIC_SIZE 64
#define ANGLE_SENSOR_TYPE_SIZE 32

typedef struct {
  /* GPIO the angle sensor is wired to, or -1 while still unknown. */
  int sensor_gpio_number;
  int sensor_sample_period_ms;
  int sensor_samples_per_reading;
  int sensor_minimum_millivolts;
  int sensor_maximum_millivolts;
  int sensor_deadband_millivolt;
  float sensor_minimum_degrees;
  float sensor_maximum_degrees;
  float sensor_center_degrees;
  char sensor_topic[ANGLE_SENSOR_CONFIG_TOPIC_SIZE];
  char sensor_type[ANGLE_SENSOR_TYPE_SIZE];
} angle_sensor_config_t;

/** Initialize one configuration from the defaults for a supported type. */
bool angle_sensor_config_init(angle_sensor_config_t *configuration,
                              const char *type);

/** Validate and atomically apply a JSON overlay to one configuration. */
esp_err_t angle_sensor_config_apply_json(angle_sensor_config_t *configuration,
                                         const cJSON *json);

/* Expand the configured voltage range to include observed calibration limits.
 * The replacement flags are set only for bounds that were changed. */
void angle_sensor_config_apply_calibration(angle_sensor_config_t *configuration,
                                           int calibration_min_millivolts,
                                           int calibration_max_millivolts,
                                           bool *minimum_replaced,
                                           bool *maximum_replaced);

#ifdef __cplusplus
}
#endif
