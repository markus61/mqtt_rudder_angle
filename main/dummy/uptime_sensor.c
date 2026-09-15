#include "uptime_sensor.h"

#include <strings.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_service.h"
#include "sensor_config.h"
#include "uptime_sensor_config.h"

static TaskHandle_t uptime_sensor_task_handle;

static bool publish_uptime_reading(const char *topic, float uptime_seconds) {
  return mqtt_publish_reading(topic, "uptime", uptime_seconds);
}

static void uptime_sensor_task(void *task_argument) {
  TickType_t last_wake_time = xTaskGetTickCount();

  while (true) {
    const uptime_sensor_config_t *configuration = uptime_sensor_config();
    const float uptime_seconds = (float)esp_timer_get_time() / 1000000.0f;
    (void)publish_uptime_reading(configuration->sensor_topic, uptime_seconds);

    vTaskDelayUntil(
        &last_wake_time,
        pdMS_TO_TICKS((uint32_t)configuration->interval_seconds * 1000U));
  }
}

esp_err_t uptime_sensor_start(void) {
  if (uptime_sensor_task_handle != NULL) {
    return ESP_OK;
  }
  ESP_LOGI("uptime_sensor", "Starting uptime sensor task...");
  if (xTaskCreate(uptime_sensor_task, "uptime_sensor", 3072, NULL, 5,
                  &uptime_sensor_task_handle) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

/** Parse and dispatch an MQTT control-action JSON payload. */
void uptime_sensor_control_action(const char *payload, int payload_length) {
  cJSON *action_json = cJSON_ParseWithLength(payload, (size_t)payload_length);
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(action_json, "action");
  if (!cJSON_IsObject(action_json) || !cJSON_IsString(action) ||
      action->valuestring == NULL) {
    ESP_LOGW("uptime_sensor", "Control payload must contain a string action");
  } else if (strcasecmp(action->valuestring, "configure_feature") == 0) {
    const char *type = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(action_json, "type"));
    const esp_err_t err = uptime_sensor_can_serve_type(type)
                              ? sensor_config_from_mqtt(action_json)
                              : ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) {
      ESP_LOGW("uptime_sensor", "Could not configure provider: %s",
               esp_err_to_name(err));
    }
  } else if (strcasecmp(action->valuestring, "braindump") == 0) {
    mqtt_publish_provider_braindump("uptime_sensor");
  } else if (strcasecmp(action->valuestring, "help") == 0) {
    mqtt_publish_provider_message(
        "uptime_sensor",
        "Publishes elapsed device uptime periodically. Start it with a "
        "configure_feature action using type 'dummy_uptime', a name, an "
        "interval in seconds, and an optional sensor_topic.");
  } else if (strcasecmp(action->valuestring, "reset") == 0) {
    cJSON_Delete(action_json);
    esp_restart();
    return;
  } else {
    ESP_LOGW("uptime_sensor", "Unknown control action '%s'", action->valuestring);
  }
  cJSON_Delete(action_json);
}

bool uptime_sensor_config_to_json(json_gen_str_t *json) { return true; }
