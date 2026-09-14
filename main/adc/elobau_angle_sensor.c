#include "elobau_angle_sensor.h"
#include "json_utils.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <strings.h>

#include "cJSON.h"
#include "elobau_angle_sensor_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_service.h"
#include "sensor_config.h"

static const char *TAG = "angle_sensor";

/* Observed voltage limits persist for the application's lifetime.  Sentinel
 * values ensure the first successful reading establishes both limits. */
static int calibration_min_millivolts = INT_MAX;
static int calibration_max_millivolts = INT_MIN;

void angle_sensor_control_action(const char *payload, int payload_length) {
  cJSON *action_json = cJSON_ParseWithLength(payload, (size_t)payload_length);
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(action_json, "action");
  if (!cJSON_IsObject(action_json) || !cJSON_IsString(action) ||
      action->valuestring == NULL) {
    ESP_LOGW(TAG, "Control payload must contain a string action");
  } else if (strcasecmp(action->valuestring, "configure_feature") == 0) {
    const char *type = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(action_json, "type"));
    const esp_err_t err = angle_sensor_can_serve_type(type)
                              ? sensor_config_from_mqtt(action_json)
                              : ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Could not configure provider: %s", esp_err_to_name(err));
    }
  } else if (strcasecmp(action->valuestring, "calibrate") == 0) {
    bool minimum_replaced = false;
    bool maximum_replaced = false;
    angle_sensor_config_apply_calibration(
        calibration_min_millivolts, calibration_max_millivolts,
        &minimum_replaced, &maximum_replaced);
    if (minimum_replaced) {
      mqtt_publish_provider_calibration("angle_sensor",
                                        "sensor_minimum_millivolts",
                                        calibration_min_millivolts);
    }
    if (maximum_replaced) {
      mqtt_publish_provider_calibration("angle_sensor",
                                        "sensor_maximum_millivolts",
                                        calibration_max_millivolts);
    }
  } else if (strcasecmp(action->valuestring, "braindump") == 0) {
    mqtt_publish_provider_braindump("angle_sensor");
  } else if (strcasecmp(action->valuestring, "help") == 0) {
    mqtt_publish_provider_message(
        "angle_sensor",
        "Reads an Elobau angle sensor through the ADC and publishes its "
        "angle in degrees. Start it with a configure_feature action using a "
        "supported Elobau type, a name, sensor_pin, sensor_topic, "
        "sensor_sample_period_ms, and sensor_samples_per_reading. Use "
        "calibrate to apply observed voltage limits.");
  } else if (strcasecmp(action->valuestring, "reset") == 0) {
    cJSON_Delete(action_json);
    esp_restart();
    return;
  } else {
    ESP_LOGW(TAG, "Unknown control action '%s'", action->valuestring);
  }
  cJSON_Delete(action_json);
}

static adc_oneshot_unit_handle_t adc_unit_handle;
static adc_cali_handle_t adc_calibration_handle;
static adc_channel_t sensor_adc_channel;
/* Most recent angle, kept so the current reading is always available. */
static float last_angle_degrees;
/* Angle at the last publish. NAN until the first one, which makes the first
 * reading always exceed the deadband and therefore always publish. */
static float last_published_angle_degrees = NAN;

/* INT_MIN means the configured voltage midpoint is still in use. */
static volatile int centered_reference_millivolts = INT_MIN;

static TaskHandle_t angle_sensor_task_handle;

/* Conversion and publish policy are sensor behaviour, not configuration.
 * The configuration document only supplies wiring, output and sampling. */
#define SENSOR_MINIMUM_MILLIVOLTS 1208
#define SENSOR_MAXIMUM_MILLIVOLTS 3020
#define SENSOR_MINIMUM_DEGREES 0.0f
#define SENSOR_MAXIMUM_DEGREES 40.0f
#define SENSOR_PUBLISH_DEADBAND_DEGREES 0.5f
/* Uses a trimmed average of raw readings and converts it to millivolts. */
static bool read_sensor_millivolts(int *out_millivolts,
                                   int samples_per_reading) {
  int raw_total = 0;
  int lowest_raw_reading = INT_MAX;
  int highest_raw_reading = INT_MIN;

  for (int sample_index = 0; sample_index < samples_per_reading;
       sample_index++) {
    int raw_reading = 0;
    esp_err_t err =
        adc_oneshot_read(adc_unit_handle, sensor_adc_channel, &raw_reading);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(err));
      return false;
    }
    raw_total += raw_reading;
    if (raw_reading < lowest_raw_reading) {
      lowest_raw_reading = raw_reading;
    }
    if (raw_reading > highest_raw_reading) {
      highest_raw_reading = raw_reading;
    }
  }

  const int averaged_raw_reading =
      (raw_total - lowest_raw_reading - highest_raw_reading) /
      (samples_per_reading - 2);

  esp_err_t err = adc_cali_raw_to_voltage(adc_calibration_handle,
                                          averaged_raw_reading, out_millivolts);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ADC calibration failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

static void angle_sensor_task(void *task_argument) {
  TickType_t last_wake_time = xTaskGetTickCount();

  while (true) {
    /* Re-read the configuration every iteration so a topic that arrives
     * later takes effect without a restart. */
    const angle_sensor_config_t *config = angle_sensor_config_get();
    const int samples_per_reading = config->sensor_samples_per_reading < 3
                                        ? 3
                                        : config->sensor_samples_per_reading;
    const float mv_per_degree =
        (float)(config->sensor_maximum_millivolts -
                config->sensor_minimum_millivolts) /
        (config->sensor_maximum_degrees - config->sensor_minimum_degrees);
    int millivolts = 0;
    if (read_sensor_millivolts(&millivolts, samples_per_reading)) {
      if (millivolts < config->sensor_minimum_millivolts) {
        calibration_min_millivolts = millivolts;
        millivolts = config->sensor_minimum_millivolts;
      }
      if (millivolts > config->sensor_maximum_millivolts) {
        calibration_max_millivolts = millivolts;
        millivolts = config->sensor_maximum_millivolts;
      }
      const int mv_convert = millivolts - config->sensor_minimum_millivolts;
      last_angle_degrees = (float)mv_convert / mv_per_degree;

      /* isnan covers the very first reading, where there is nothing to
       * compare against yet. */
      const bool angle_changed =
          isnan(last_published_angle_degrees) ||
          fabsf(last_angle_degrees - last_published_angle_degrees) >=
              SENSOR_PUBLISH_DEADBAND_DEGREES;

      if (angle_changed && mqtt_publish_sensor_reading(config->sensor_topic,
                                                       last_angle_degrees)) {
        /* Only advance the reference once the reading actually went
         * out, so a publish refused while the broker is unreachable is
         * retried on the next sample. */
        last_published_angle_degrees = last_angle_degrees;
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
static esp_err_t create_adc_calibration(void) {
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
                                             &adc_calibration_handle);
}

esp_err_t angle_sensor_start(void) {
  if (angle_sensor_task_handle != NULL) {
    ESP_LOGI(TAG, "Angle sensor is already running");
    return ESP_OK;
  }

  const angle_sensor_config_t *config = angle_sensor_config_get();
  if (config->sensor_gpio_number < 0) {
    ESP_LOGI(TAG, "No sensor pin configured yet, not sampling");
    return ESP_ERR_INVALID_STATE;
  }

  /* The pin was validated as an ADC1 pin when the configuration was applied,
   * so this only needs the channel it maps onto. */
  adc_unit_t resolved_adc_unit = ADC_UNIT_1;
  esp_err_t err = adc_oneshot_io_to_channel(
      config->sensor_gpio_number, &resolved_adc_unit, &sensor_adc_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPIO %d is not an ADC pin: %s", config->sensor_gpio_number,
             esp_err_to_name(err));
    return err;
  }

  adc_oneshot_unit_init_cfg_t unit_config = {
      .unit_id = ADC_UNIT_1,
  };
  err = adc_oneshot_new_unit(&unit_config, &adc_unit_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialise ADC1: %s", esp_err_to_name(err));
    return err;
  }

  /* 12 dB attenuation gives the widest input range, which suits a sensor
   * swinging across the full supply. */
  adc_oneshot_chan_cfg_t channel_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  err = adc_oneshot_config_channel(adc_unit_handle, sensor_adc_channel,
                                   &channel_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(err));
    goto release_adc_unit;
  }

  err = create_adc_calibration();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to characterise the ADC: %s", esp_err_to_name(err));
    goto release_adc_unit;
  }

  ESP_LOGI(TAG, "Starting the angle sensor task");
  if (xTaskCreate(angle_sensor_task, "angle_sensor", 4096, NULL, 5,
                  &angle_sensor_task_handle) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create the angle sensor task");
    err = ESP_ERR_NO_MEM;
    goto release_adc_calibration;
  }

  ESP_LOGI(TAG, "Sampling GPIO %d at %d Hz, publishing to '%s'",
           config->sensor_gpio_number, 1000 / config->sensor_sample_period_ms,
           config->sensor_topic);

  mqtt_publish_sensor_reading(config->sensor_topic, last_angle_degrees);

  return ESP_OK;

release_adc_calibration:
  adc_cali_delete_scheme_line_fitting(adc_calibration_handle);
  adc_calibration_handle = NULL;
release_adc_unit:
  adc_oneshot_del_unit(adc_unit_handle);
  adc_unit_handle = NULL;
  return err;
}

bool angle_sensor_config_to_json(json_gen_str_t *json) {
  const angle_sensor_config_t *config = angle_sensor_config_get();
  return json != NULL && config != NULL &&
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
         json_gen_obj_set_float(json, "sensor_minimum_degrees",
                                config->sensor_minimum_degrees) == 0 &&
         json_gen_obj_set_float(json, "sensor_maximum_degrees",
                                config->sensor_maximum_degrees) == 0 &&
         json_gen_obj_set_float(json, "sensor_center_degrees",
                                config->sensor_center_degrees) == 0 &&
         json_obj_set_escaped_string(json, "sensor_topic",
                                     config->sensor_topic);
}
