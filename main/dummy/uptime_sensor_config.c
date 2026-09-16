#include "uptime_sensor.h"

#include <limits.h>
#include <string.h>

#include "json_utils.h"
#include "uptime_sensor_config.h"

static const char *const UPTIME_SENSOR_TYPES[] = {
    "dummy_uptime",
};

static uptime_sensor_config_t configuration = {
    .interval_seconds = 60,
    .sensor_topic = "sensors/uptime",
    .sensor_type = "dummy_uptime",
};

const uptime_sensor_config_t *uptime_sensor_config(void) {
  return &configuration;
}

const void *uptime_sensor_config_get(void) { return &configuration; }

size_t uptime_sensor_config_size(void) { return sizeof(configuration); }

esp_err_t uptime_sensor_config_restore(const void *record) {
  if (record == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  memcpy(&configuration, record, sizeof(configuration));
  return ESP_OK;
}

bool uptime_sensor_can_serve_type(const char *type) {
  if (type == NULL) {
    return false;
  }

  for (size_t index = 0U;
       index < sizeof(UPTIME_SENSOR_TYPES) / sizeof(UPTIME_SENSOR_TYPES[0]);
       ++index) {
    if (strcmp(type, UPTIME_SENSOR_TYPES[index]) == 0) {
      return true;
    }
  }

  return false;
}

size_t uptime_sensor_supported_type_count(void) {
  return sizeof(UPTIME_SENSOR_TYPES) / sizeof(UPTIME_SENSOR_TYPES[0]);
}

const char *uptime_sensor_supported_type(size_t index) {
  return index < uptime_sensor_supported_type_count() ? UPTIME_SENSOR_TYPES[index]
                                                       : NULL;
}

esp_err_t uptime_sensor_configure(const cJSON *configuration_json) {
  if (!cJSON_IsObject(configuration_json)) {
    return ESP_ERR_INVALID_ARG;
  }

  const char *type = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(configuration_json, "type"));
  if (type != NULL && strcmp(type, configuration.sensor_type) != 0) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *interval =
      cJSON_GetObjectItemCaseSensitive(configuration_json, "interval");
  if (interval != NULL && (!cJSON_IsNumber(interval) || interval->valueint <= 0 ||
                           interval->valueint > INT_MAX / 1000)) {
    return ESP_ERR_INVALID_ARG;
  }

  const char *topic = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(configuration_json, "sensor_topic"));
  if (topic != NULL && (topic[0] == '\0' ||
                        strlen(topic) >= sizeof(configuration.sensor_topic))) {
    return ESP_ERR_INVALID_ARG;
  }

  uptime_sensor_config_t candidate = configuration;
  if (interval != NULL) {
    candidate.interval_seconds = interval->valueint;
  }
  if (topic != NULL) {
    strlcpy(candidate.sensor_topic, topic, sizeof(candidate.sensor_topic));
  }
  configuration = candidate;
  return ESP_OK;
}
