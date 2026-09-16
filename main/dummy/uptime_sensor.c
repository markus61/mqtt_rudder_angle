#include "uptime_sensor.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "json_utils.h"
#include "mqtt_service.h"
#include "sensor_config.h"
#include "uptime_sensor_config.h"

typedef struct {
  uptime_sensor_config_t configuration;
  TaskHandle_t task_handle;
} uptime_sensor_instance_t;

static const char *TAG = "uptime_sensor";

void *uptime_sensor_create(const char *type) {
  uptime_sensor_instance_t *instance = calloc(1, sizeof(*instance));
  if (instance == NULL ||
      !uptime_sensor_config_init(&instance->configuration, type)) {
    free(instance);
    return NULL;
  }
  return instance;
}

void uptime_sensor_destroy(void *opaque) {
  uptime_sensor_instance_t *instance = opaque;
  if (instance != NULL && instance->task_handle != NULL) {
    vTaskDelete(instance->task_handle);
  }
  free(instance);
}

const void *uptime_sensor_config_get(const void *opaque) {
  const uptime_sensor_instance_t *instance = opaque;
  return instance != NULL ? &instance->configuration : NULL;
}

size_t uptime_sensor_config_size(void) { return sizeof(uptime_sensor_config_t); }

esp_err_t uptime_sensor_configure(void *opaque, const cJSON *configuration) {
  uptime_sensor_instance_t *instance = opaque;
  return instance != NULL
             ? uptime_sensor_config_apply_json(&instance->configuration,
                                               configuration)
             : ESP_ERR_INVALID_ARG;
}

esp_err_t uptime_sensor_config_restore(void *opaque,
                                       const void *configuration) {
  uptime_sensor_instance_t *instance = opaque;
  if (instance == NULL || configuration == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const uptime_sensor_config_t *record = configuration;
  if (memchr(record->sensor_type, '\0', sizeof(record->sensor_type)) == NULL ||
      strcmp(record->sensor_type, instance->configuration.sensor_type) != 0) {
    return ESP_ERR_INVALID_ARG;
  }
  memcpy(&instance->configuration, configuration,
         sizeof(instance->configuration));
  return ESP_OK;
}

static bool publish_uptime_reading(const char *topic, float uptime_seconds) {
  char payload[128];
  time_t current_time = time(NULL);
  struct tm utc_time = {0};
  gmtime_r(&current_time, &utc_time);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc_time);
  json_gen_str_t generator;
  json_gen_str_start(&generator, payload, sizeof(payload), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "now", timestamp) ||
      json_gen_obj_set_float(&generator, "uptime", uptime_seconds) != 0 ||
      json_gen_end_object(&generator) != 0) {
    return false;
  }
  const int length = json_gen_str_end(&generator);
  return length > 1 && (size_t)length <= sizeof(payload) &&
         mqtt_publish_telemetry(topic, payload, (size_t)length - 1U);
}

static void uptime_sensor_task(void *argument) {
  uptime_sensor_instance_t *instance = argument;
  TickType_t last_wake_time = xTaskGetTickCount();
  while (true) {
    const uptime_sensor_config_t *configuration = &instance->configuration;
    const float uptime_seconds = (float)esp_timer_get_time() / 1000000.0f;
    (void)publish_uptime_reading(configuration->sensor_topic, uptime_seconds);
    vTaskDelayUntil(
        &last_wake_time,
        pdMS_TO_TICKS((uint32_t)configuration->interval_seconds * 1000U));
  }
}

esp_err_t uptime_sensor_start(void *opaque) {
  uptime_sensor_instance_t *instance = opaque;
  if (instance == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (instance->task_handle != NULL) {
    return ESP_OK;
  }
  if (xTaskCreate(uptime_sensor_task, "uptime_sensor", 3072, instance, 5,
                  &instance->task_handle) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

static bool publish_reply(const char *name, const char *payload,
                          size_t length) {
  return mqtt_publish_sensor_reply(name, payload, length);
}

static void publish_message(const char *name, const char *message) {
  char payload[512];
  json_gen_str_t generator;
  json_gen_str_start(&generator, payload, sizeof(payload), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "message", message) ||
      json_gen_end_object(&generator) != 0) {
    return;
  }
  const int length = json_gen_str_end(&generator);
  if (length <= 1 || (size_t)length > sizeof(payload) ||
      !publish_reply(name, payload, (size_t)length - 1U)) {
    ESP_LOGW(TAG, "Could not publish help response");
  }
}

static void publish_braindump(const uptime_sensor_instance_t *instance,
                              const char *name) {
  char payload[1024];
  json_gen_str_t generator;
  json_gen_str_start(&generator, payload, sizeof(payload), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "name", name) ||
      !json_obj_set_escaped_string(&generator, "type",
                                   instance->configuration.sensor_type) ||
      uptime_sensor_config_to_json(instance, &generator) != ESP_OK ||
      json_gen_end_object(&generator) != 0) {
    return;
  }
  const int length = json_gen_str_end(&generator);
  if (length <= 1 || (size_t)length > sizeof(payload) ||
      !publish_reply(name, payload, (size_t)length - 1U)) {
    ESP_LOGW(TAG, "Could not publish provider configuration");
  }
}

esp_err_t uptime_sensor_control_action(void *opaque, const char *instance_name,
                                       const char *payload,
                                       int payload_length) {
  uptime_sensor_instance_t *instance = opaque;
  if (instance == NULL || instance_name == NULL || payload == NULL ||
      payload_length < 0) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t result = ESP_OK;
  cJSON *json = cJSON_ParseWithLength(payload, (size_t)payload_length);
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
  if (!cJSON_IsObject(json) || !cJSON_IsString(action) ||
      action->valuestring == NULL) {
    result = ESP_ERR_INVALID_ARG;
  } else if (strcasecmp(action->valuestring, "configure") == 0) {
    result = sensor_provider_configure_from_mqtt(instance_name, json);
  } else if (strcasecmp(action->valuestring, "braindump") == 0) {
    publish_braindump(instance, instance_name);
  } else if (strcasecmp(action->valuestring, "help") == 0) {
    publish_message(instance_name,
                    "Publishes elapsed device uptime periodically. Configure "
                    "it with a unique safe name, an optional interval, and an "
                    "optional sensor_topic.");
  } else {
    result = ESP_ERR_INVALID_ARG;
  }
  cJSON_Delete(json);
  return result;
}

esp_err_t uptime_sensor_config_to_json(const void *opaque,
                                       json_gen_str_t *json) {
  const uptime_sensor_instance_t *instance = opaque;
  if (instance == NULL || json == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return json_gen_obj_set_int(json, "interval",
                              instance->configuration.interval_seconds) == 0 &&
                 json_obj_set_escaped_string(
                     json, "sensor_topic", instance->configuration.sensor_topic)
             ? ESP_OK
             : ESP_FAIL;
}

esp_err_t uptime_sensor_working_topics_json_add(const void *opaque,
                                                json_gen_str_t *json) {
  const uptime_sensor_instance_t *instance = opaque;
  if (instance == NULL || json == NULL ||
      instance->configuration.sensor_topic[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  return json_gen_arr_set_string(json, instance->configuration.sensor_topic) == 0
             ? ESP_OK
             : ESP_FAIL;
}
