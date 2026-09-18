#include "elobau_angle_sensor.h"
#include "json_utils.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "elobau_angle_sensor_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_service.h"
#include "sensor_config.h"

static const char *TAG = "angle_sensor";

typedef struct {
  angle_sensor_config_t configuration;
  int calibration_min_millivolts;
  int calibration_max_millivolts;
  adc_cali_handle_t adc_calibration_handle;
  adc_channel_t sensor_adc_channel;
  float last_angle_degrees;
  float last_published_millivolts;
  TaskHandle_t task_handle;
} angle_sensor_instance_t;

/* ADC1 is a device resource. Instances own channels and calibration state,
 * while the one-shot unit is shared so different pins can run concurrently. */
static adc_oneshot_unit_handle_t shared_adc_unit_handle;
static size_t shared_adc_users;

void *angle_sensor_create(const char *type) {
  angle_sensor_instance_t *instance = calloc(1, sizeof(*instance));
  if (instance == NULL ||
      !angle_sensor_config_init(&instance->configuration, type)) {
    free(instance);
    return NULL;
  }
  instance->calibration_min_millivolts = INT_MAX;
  instance->calibration_max_millivolts = INT_MIN;
  instance->last_published_millivolts = NAN;
  return instance;
}

void angle_sensor_destroy(void *opaque) {
  angle_sensor_instance_t *instance = opaque;
  if (instance == NULL) {
    return;
  }
  const bool used_adc = instance->task_handle != NULL;
  if (used_adc) {
    vTaskDelete(instance->task_handle);
  }
  if (instance->adc_calibration_handle != NULL) {
    adc_cali_delete_scheme_line_fitting(instance->adc_calibration_handle);
  }
  if (used_adc && shared_adc_users > 0U && --shared_adc_users == 0U &&
      shared_adc_unit_handle != NULL) {
    adc_oneshot_del_unit(shared_adc_unit_handle);
    shared_adc_unit_handle = NULL;
  }
  free(instance);
}

const void *angle_sensor_config_get(const void *opaque) {
  const angle_sensor_instance_t *instance = opaque;
  return instance != NULL ? &instance->configuration : NULL;
}

size_t angle_sensor_config_size(void) { return sizeof(angle_sensor_config_t); }

esp_err_t angle_sensor_configure(void *opaque, const cJSON *configuration) {
  angle_sensor_instance_t *instance = opaque;
  return instance != NULL
             ? angle_sensor_config_apply_json(&instance->configuration,
                                              configuration)
             : ESP_ERR_INVALID_ARG;
}

esp_err_t angle_sensor_config_restore(void *opaque,
                                      const void *configuration) {
  angle_sensor_instance_t *instance = opaque;
  if (instance == NULL || configuration == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const angle_sensor_config_t *record = configuration;
  if (memchr(record->sensor_type, '\0', sizeof(record->sensor_type)) == NULL ||
      strcmp(record->sensor_type, instance->configuration.sensor_type) != 0) {
    return ESP_ERR_INVALID_ARG;
  }
  memcpy(&instance->configuration, configuration,
         sizeof(instance->configuration));
  return ESP_OK;
}

static bool publish_angle_reading(const char *topic, float angle_degrees) {
  char payload[128];
  time_t current_time = time(NULL);
  struct tm utc_time = {0};
  gmtime_r(&current_time, &utc_time);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc_time);

  const int length = snprintf(payload, sizeof(payload),
                              "{\"now\":\"%s\",\"angle\":%.1f}", timestamp,
                              (double)angle_degrees);
  return length > 1 && (size_t)length < sizeof(payload) &&
         mqtt_publish_telemetry(topic, payload, (size_t)length);
}

static void publish_angle_reply_message(const char *instance_name,
                                        const char *message) {
  char payload[512];
  json_gen_str_t generator;
  json_gen_str_start(&generator, payload, sizeof(payload), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "message", message) ||
      json_gen_end_object(&generator) != 0) {
    ESP_LOGW(TAG, "Help response is too large");
    return;
  }
  const int length = json_gen_str_end(&generator);
  if (length <= 1 || (size_t)length > sizeof(payload) ||
      !mqtt_publish_sensor_reply(instance_name, payload,
                                   (size_t)length - 1U)) {
    ESP_LOGW(TAG, "Could not publish help response");
  }
}

static void publish_angle_braindump(const angle_sensor_instance_t *instance,
                                    const char *instance_name) {
  char payload[1024];
  const angle_sensor_config_t *config = &instance->configuration;
  json_gen_str_t generator;
  json_gen_str_start(&generator, payload, sizeof(payload), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "name", instance_name) ||
      !json_obj_set_escaped_string(&generator, "type", config->sensor_type) ||
      angle_sensor_config_to_json(instance, &generator) != ESP_OK ||
      json_gen_end_object(&generator) != 0) {
    ESP_LOGW(TAG, "Provider configuration is too large");
    return;
  }
  const int length = json_gen_str_end(&generator);
  if (length <= 1 || (size_t)length > sizeof(payload) ||
      !mqtt_publish_sensor_reply(instance_name, payload,
                                   (size_t)length - 1U)) {
    ESP_LOGW(TAG, "Could not publish provider configuration");
  }
}

static void publish_angle_calibration(const char *instance_name,
                                      const char *field, int value) {
  char payload[128];
  json_gen_str_t generator;
  json_gen_str_start(&generator, payload, sizeof(payload), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      !json_obj_set_escaped_string(&generator, "action", "calibration") ||
      json_gen_obj_set_int(&generator, field, value) != 0 ||
      json_gen_end_object(&generator) != 0) {
    ESP_LOGW(TAG, "Calibration response is too large");
    return;
  }
  const int length = json_gen_str_end(&generator);
  if (length <= 1 || (size_t)length > sizeof(payload) ||
      !mqtt_publish_sensor_reply(instance_name, payload,
                                     (size_t)length - 1U)) {
    ESP_LOGW(TAG, "Could not publish calibration response");
  }
}

static void publish_angle_calibration_check(const char *instance_name,
                                            bool calibration_required,
                                            int calibration_min_value,
                                            int calibration_max_value) {
  char payload[160];
  json_gen_str_t generator;
  json_gen_str_start(&generator, payload, sizeof(payload), NULL, NULL);
  if (json_gen_start_object(&generator) != 0 ||
      json_gen_obj_set_bool(&generator, "calibration_required",
                            calibration_required) != 0 ||
      json_gen_obj_set_int(&generator, "calibration_min_value",
                           calibration_min_value) != 0 ||
      json_gen_obj_set_int(&generator, "calibration_max_value",
                           calibration_max_value) != 0 ||
      json_gen_end_object(&generator) != 0) {
    ESP_LOGW(TAG, "Calibration-check response is too large");
    return;
  }
  const int length = json_gen_str_end(&generator);
  if (length <= 1 || (size_t)length > sizeof(payload) ||
      !mqtt_publish_sensor_reply(instance_name, payload,
                                   (size_t)length - 1U)) {
    ESP_LOGW(TAG, "Could not publish calibration-check response");
  }
}

esp_err_t angle_sensor_control_action(void *opaque, const char *instance_name,
                                      const char *payload, int payload_length) {
  angle_sensor_instance_t *instance = opaque;
  if (instance == NULL || instance_name == NULL || payload == NULL ||
      payload_length < 0) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t result = ESP_OK;
  cJSON *action_json = cJSON_ParseWithLength(payload, (size_t)payload_length);
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(action_json, "action");
  if (!cJSON_IsObject(action_json) || !cJSON_IsString(action) ||
      action->valuestring == NULL) {
    ESP_LOGW(TAG, "Control payload must contain a string action");
    result = ESP_ERR_INVALID_ARG;
  } else if (strcasecmp(action->valuestring, "configure") == 0) {
    const esp_err_t err = sensor_provider_configure_from_mqtt(
        instance_name, action_json);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Could not configure sensor: %s", esp_err_to_name(err));
    }
    result = err;
  } else if (strcasecmp(action->valuestring, "calibrate") == 0) {
    bool minimum_replaced = false;
    bool maximum_replaced = false;
    angle_sensor_config_apply_calibration(&instance->configuration,
                                          instance->calibration_min_millivolts,
                                          instance->calibration_max_millivolts,
                                          &minimum_replaced, &maximum_replaced);
    if (minimum_replaced) {
      publish_angle_calibration(instance_name, "sensor_minimum_millivolts",
                                instance->calibration_min_millivolts);
    }
    if (maximum_replaced) {
      publish_angle_calibration(instance_name, "sensor_maximum_millivolts",
                                instance->calibration_max_millivolts);
    }
  } else if (strcasecmp(action->valuestring, "calibration_check") == 0) {
    const angle_sensor_config_t *config = &instance->configuration;
    const bool calibration_required =
        instance->calibration_min_millivolts < config->sensor_minimum_millivolts ||
        instance->calibration_max_millivolts > config->sensor_maximum_millivolts;
    publish_angle_calibration_check(instance_name, calibration_required,
                                    instance->calibration_min_millivolts,
                                    instance->calibration_max_millivolts);
  } else if (strcasecmp(action->valuestring, "braindump") == 0) {
    publish_angle_braindump(instance, instance_name);
  } else if (strcasecmp(action->valuestring, "help") == 0) {
    publish_angle_reply_message(instance_name,
        "Reads an Elobau angle sensor through the ADC and publishes its "
        "angle in degrees. Configure it with a safe name; this moves control "
        "and replies from its number to that "
        "name. Every sensor_* setting is optional, and omitted settings "
        "retain their current values, while an invalid supplied setting "
        "rejects the entire action. Use "
        "calibration_check to inspect observed voltage limits, or calibrate "
        "to apply them. Device braindump reports its name and working topics.");
  } else {
    char reply[256];
    json_gen_str_t generator;
    json_gen_str_start(&generator, reply, sizeof(reply), NULL, NULL);
    if (json_gen_start_object(&generator) != 0 ||
        !json_obj_set_escaped_string(&generator, "unknown_action",
                                     action->valuestring) ||
        json_gen_push_array(&generator, "available_actions") != 0 ||
        json_gen_arr_set_string(&generator, "configure") != 0 ||
        json_gen_arr_set_string(&generator, "calibrate") != 0 ||
        json_gen_arr_set_string(&generator, "calibration_check") != 0 ||
        json_gen_arr_set_string(&generator, "braindump") != 0 ||
        json_gen_arr_set_string(&generator, "help") != 0 ||
        json_gen_pop_array(&generator) != 0 ||
        json_gen_end_object(&generator) != 0) {
      ESP_LOGW(TAG, "Unknown-action response is too large");
    } else {
      const int length = json_gen_str_end(&generator);
      if (length <= 1 || (size_t)length > sizeof(reply) ||
          !mqtt_publish_sensor_reply(instance_name, reply,
                                       (size_t)length - 1U)) {
        ESP_LOGW(TAG, "Could not publish unknown-action response");
      }
    }
    ESP_LOGW(TAG, "Unknown control action '%s'", action->valuestring);
    result = ESP_ERR_INVALID_ARG;
  }
  cJSON_Delete(action_json);
  return result;
}

/* Uses a trimmed average of raw readings and converts it to millivolts. */
static bool read_sensor_millivolts(angle_sensor_instance_t *instance,
                                   int *out_millivolts,
                                   int samples_per_reading) {
  int total = 0;
  int lowest_reading = INT_MAX;
  int highest_reading = INT_MIN;

  for (int sample_index = 0; sample_index < samples_per_reading;
       sample_index++) {
    int raw_reading = 0;
    esp_err_t err =
        adc_oneshot_read(shared_adc_unit_handle, instance->sensor_adc_channel,
                         &raw_reading);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(err));
      return false;
    }
    int millivolts = 0;
    err = adc_cali_raw_to_voltage(instance->adc_calibration_handle, raw_reading,
                                  &millivolts);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "ADC calibration failed: %s", esp_err_to_name(err));
    }
    total += millivolts;
    if (millivolts < lowest_reading) {
      lowest_reading = millivolts;
    }
    if (millivolts > highest_reading) {
      highest_reading = millivolts;
    }
  }

  const int averaged_millivolts =
      (total - lowest_reading - highest_reading) / (samples_per_reading - 2);

  *out_millivolts = averaged_millivolts;
  return true;
}

static void angle_sensor_task(void *task_argument) {
  angle_sensor_instance_t *instance = task_argument;
  TickType_t last_wake_time = xTaskGetTickCount();
  float degrees = 0.0;

  while (true) {
    /* Re-read the configuration every iteration so a topic that arrives
     * later takes effect without a restart. */
    const angle_sensor_config_t *config = &instance->configuration;
    const int samples_per_reading = config->sensor_samples_per_reading < 3
                                        ? 3
                                        : config->sensor_samples_per_reading;
    const float mv_per_degree =
        (float)(config->sensor_maximum_millivolts -
                config->sensor_minimum_millivolts) /
        (config->sensor_maximum_degrees - config->sensor_minimum_degrees);
    int millivolts = 0;
    if (read_sensor_millivolts(instance, &millivolts, samples_per_reading)) {
      /* Retain the raw observed range for calibration, independent of the
       * configured range used to convert this reading into an angle. */
      if (millivolts < instance->calibration_min_millivolts) {
        instance->calibration_min_millivolts = millivolts;
      }
      if (millivolts > instance->calibration_max_millivolts) {
        instance->calibration_max_millivolts = millivolts;
      }
      if (millivolts < config->sensor_minimum_millivolts) {
        millivolts = config->sensor_minimum_millivolts;
      }
      if (millivolts > config->sensor_maximum_millivolts) {
        millivolts = config->sensor_maximum_millivolts;
      }

      /* isnan covers the very first reading, where there is nothing to
       * compare against yet. */
      if ((isnan(instance->last_published_millivolts) ||
           fabsf(millivolts - instance->last_published_millivolts) >=
               config->sensor_deadband_millivolt)) {

        const int mv_convert = millivolts - config->sensor_minimum_millivolts;
        degrees = roundf(((float)mv_convert / mv_per_degree -
                          config->sensor_center_degrees) *
                         10.0f) *
                  0.1f;

        if (publish_angle_reading(config->sensor_topic, degrees)) {
          /* Only advance the reference once the reading actually went
           * out, so a publish refused while the broker is unreachable is
           * retried on the next sample. */
          instance->last_published_millivolts = millivolts;
        }
      } else {
        ESP_LOGD(TAG, "Angle change below deadband, not publishing");
      }
    }

    /* Delay against the last wake-up rather than "now", so the conversion
     * and publish time does not accumulate into a slower sample rate. */
    vTaskDelayUntil(&last_wake_time,
                    pdMS_TO_TICKS(config->sensor_sample_period_ms));
  }
}

/* Characterises the ADC so raw counts can be turned into millivolts. On the
 * ESP32 the line fitting scheme always succeeds: without the eFuse calibration
 * bits it falls back to the nominal reference voltage, which is less accurate
 * but still usable. */
static esp_err_t create_adc_calibration(angle_sensor_instance_t *instance) {
#if CONFIG_IDF_TARGET_ESP32
  adc_cali_line_fitting_efuse_val_t efuse_calibration_value;
  if (adc_cali_scheme_line_fitting_check_efuse(&efuse_calibration_value) ==
          ESP_OK &&
      efuse_calibration_value == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
    ESP_LOGW(TAG, "No ADC calibration burnt in eFuse, using the nominal "
                  "reference voltage; readings will be less accurate");
  }
#endif

  adc_cali_line_fitting_config_t calibration_config = {
      .unit_id = ADC_UNIT_1,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
#if CONFIG_IDF_TARGET_ESP32
      .default_vref = 1100,
#endif
  };

  return adc_cali_create_scheme_line_fitting(&calibration_config,
                                             &instance->adc_calibration_handle);
}

esp_err_t angle_sensor_start(void *opaque) {
  angle_sensor_instance_t *instance = opaque;
  if (instance == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (instance->task_handle != NULL) {
    ESP_LOGI(TAG, "Angle sensor is already running");
    return ESP_OK;
  }

  const angle_sensor_config_t *config = &instance->configuration;
  if (config->sensor_gpio_number < 0) {
    /* The retained/default configuration does not yet identify a pin. */
    ESP_LOGI(TAG, "No sensor pin configured yet, provider is idle");
    return ESP_OK;
  }

  /* The pin was validated as an ADC1 pin when the configuration was applied,
   * so this only needs the channel it maps onto. */
  adc_unit_t resolved_adc_unit = ADC_UNIT_1;
  esp_err_t err = adc_oneshot_io_to_channel(
      config->sensor_gpio_number, &resolved_adc_unit,
      &instance->sensor_adc_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPIO %d is not an ADC pin: %s", config->sensor_gpio_number,
             esp_err_to_name(err));
    return err;
  }

  adc_oneshot_unit_init_cfg_t unit_config = {
      .unit_id = ADC_UNIT_1,
  };
  const bool created_adc_unit = shared_adc_unit_handle == NULL;
  if (created_adc_unit) {
    err = adc_oneshot_new_unit(&unit_config, &shared_adc_unit_handle);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialise ADC1: %s", esp_err_to_name(err));
      return err;
    }
  }

  /* 12 dB attenuation gives the widest input range, which suits a sensor
   * swinging across the full supply. */
  adc_oneshot_chan_cfg_t channel_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  err = adc_oneshot_config_channel(shared_adc_unit_handle,
                                   instance->sensor_adc_channel,
                                   &channel_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(err));
    goto release_adc_unit;
  }

  err = create_adc_calibration(instance);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to characterise the ADC: %s", esp_err_to_name(err));
    goto release_adc_unit;
  }

  ESP_LOGI(TAG, "Starting the angle sensor task");
  if (xTaskCreate(angle_sensor_task, "angle_sensor", 4096, instance, 5,
                  &instance->task_handle) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create the angle sensor task");
    err = ESP_ERR_NO_MEM;
    goto release_adc_calibration;
  }
  ++shared_adc_users;

  ESP_LOGI(TAG, "Sampling GPIO %d at %d Hz, publishing to '%s'",
           config->sensor_gpio_number, 1000 / config->sensor_sample_period_ms,
           config->sensor_topic);

  publish_angle_reading(config->sensor_topic, instance->last_angle_degrees);

  return ESP_OK;

release_adc_calibration:
  adc_cali_delete_scheme_line_fitting(instance->adc_calibration_handle);
  instance->adc_calibration_handle = NULL;
release_adc_unit:
  if (created_adc_unit && shared_adc_users == 0U) {
    adc_oneshot_del_unit(shared_adc_unit_handle);
    shared_adc_unit_handle = NULL;
  }
  return err;
}

esp_err_t angle_sensor_config_to_json(const void *opaque, json_gen_str_t *json) {
  const angle_sensor_instance_t *instance = opaque;
  const angle_sensor_config_t *config =
      instance != NULL ? &instance->configuration : NULL;
  if (json == NULL || config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return
         json_gen_obj_set_int(json, "sensor_pin", config->sensor_gpio_number) ==
             0 &&
         json_gen_obj_set_int(json, "sensor_samples_per_reading",
                              config->sensor_samples_per_reading) == 0 &&
         json_gen_obj_set_int(json, "sensor_sample_period_ms",
                              config->sensor_sample_period_ms) == 0 &&
         json_gen_obj_set_int(json, "sensor_minimum_millivolts",
                              config->sensor_minimum_millivolts) == 0 &&
         json_gen_obj_set_int(json, "sensor_maximum_millivolts",
                              config->sensor_maximum_millivolts) == 0 &&
         json_gen_obj_set_int(json, "sensor_deadband_millivolt",
                              config->sensor_deadband_millivolt) == 0 &&
         json_gen_obj_set_float(json, "sensor_minimum_degrees",
                                config->sensor_minimum_degrees) == 0 &&
         json_gen_obj_set_float(json, "sensor_maximum_degrees",
                                config->sensor_maximum_degrees) == 0 &&
         json_gen_obj_set_float(json, "sensor_center_degrees",
                                config->sensor_center_degrees) == 0 &&
         json_obj_set_escaped_string(json, "sensor_topic",
                                     config->sensor_topic)
             ? ESP_OK
             : ESP_FAIL;
}

esp_err_t angle_sensor_working_topics_json_add(const void *opaque,
                                               json_gen_str_t *json) {
  const angle_sensor_instance_t *instance = opaque;
  const angle_sensor_config_t *config =
      instance != NULL ? &instance->configuration : NULL;
  if (json == NULL || config == NULL || config->sensor_topic[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  return json_gen_arr_set_string(json, config->sensor_topic) == 0 ? ESP_OK
                                                                    : ESP_FAIL;
}
