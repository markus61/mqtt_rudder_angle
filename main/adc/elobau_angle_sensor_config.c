#include "elobau_angle_sensor_config.h"
#include "elobau_angle_sensor.h"
#include "json_utils.h"

#include <stdbool.h>
#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "elobau_angle_sensor_config";
static const char *const ANGLE_SENSOR_CONFIG_TYPES[] = {
    "elobau_424A11A040B",
    "elobau_424A11A060B",
};

/**
 * @brief The default configuration for the Elobau angle sensor.
 */
static angle_sensor_config_t angle_sensor_config = {
    .sensor_gpio_number = 32,
    .sensor_sample_period_ms = 100,
    .sensor_samples_per_reading = 8,
    .sensor_minimum_millivolts = 3300,
    .sensor_maximum_millivolts = 0,
    .sensor_topic = "sensors/rudders/starboard",
    .sensor_type = "elobau_424A11A040B",
    .sensor_minimum_degrees = 0.0f,
    .sensor_maximum_degrees = 40.0f,
    .sensor_center_degrees = 20.0f,
};

static void apply_positive_integer_member(const cJSON *configuration,
                                          const char *member_name,
                                          int *destination);
static void apply_integer_member(const cJSON *configuration,
                                 const char *member_name, int *destination);
static void apply_float_member(const cJSON *configuration,
                               const char *member_name, float *destination);
static void apply_sensor_pin(const cJSON *configuration);
static void apply_topic(const cJSON *configuration);
static void apply_sensor_type(const cJSON *configuration);

const void *angle_sensor_config_get(void) { return &angle_sensor_config; }

size_t angle_sensor_config_size(void) { return sizeof(angle_sensor_config); }

bool angle_sensor_can_serve_type(const char *type) {
  if (type == NULL) {
    return false;
  }

  for (size_t index = 0U; index < sizeof(ANGLE_SENSOR_CONFIG_TYPES) /
                                      sizeof(ANGLE_SENSOR_CONFIG_TYPES[0]);
       ++index) {
    if (strcmp(type, ANGLE_SENSOR_CONFIG_TYPES[index]) == 0) {
      return true;
    }
  }

  return false;
}

size_t angle_sensor_supported_type_count(void) {
  return sizeof(ANGLE_SENSOR_CONFIG_TYPES) /
         sizeof(ANGLE_SENSOR_CONFIG_TYPES[0]);
}

const char *angle_sensor_supported_type(size_t index) {
  return index < angle_sensor_supported_type_count()
             ? ANGLE_SENSOR_CONFIG_TYPES[index]
             : NULL;
}

/**
 * @brief Apply the entire angle sensor configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static void apply_configuration(const cJSON *configuration) {
  apply_sensor_type(configuration);
  angle_sensor_config_apply_json(configuration);
}

static void apply_sensor_type(const cJSON *configuration) {
  const char *type = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(configuration, "type"));
  if (type != NULL) {
    strlcpy(angle_sensor_config.sensor_type, type,
            sizeof(angle_sensor_config.sensor_type));
  }
}

bool angle_sensor_configure(const cJSON *configuration) {
  const char *type = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(configuration, "type"));

  if (!angle_sensor_can_serve_type(type)) {
    return false;
  }

  /* Sensor settings are an overlay.  A model-only configuration is useful
   * while wiring/calibration details are still unknown; omitted members keep
   * their existing (or default) values. */
  apply_configuration(configuration);
  return true;
}

void angle_sensor_config_restore(const void *configuration) {
  if (configuration != NULL) {
    memcpy(&angle_sensor_config, configuration, sizeof(angle_sensor_config));
  }
}

void angle_sensor_config_apply_calibration(int calibration_min_millivolts,
                                           int calibration_max_millivolts,
                                           bool *minimum_replaced,
                                           bool *maximum_replaced) {
  const bool replace_minimum = calibration_min_millivolts <
                               angle_sensor_config.sensor_minimum_millivolts;
  const bool replace_maximum = calibration_max_millivolts >
                               angle_sensor_config.sensor_maximum_millivolts;

  if (replace_minimum) {
    angle_sensor_config.sensor_minimum_millivolts = calibration_min_millivolts;
  }
  if (replace_maximum) {
    angle_sensor_config.sensor_maximum_millivolts = calibration_max_millivolts;
  }
  if (minimum_replaced != NULL) {
    *minimum_replaced = replace_minimum;
  }
  if (maximum_replaced != NULL) {
    *maximum_replaced = replace_maximum;
  }
}

static bool sensor_pin_is_usable(int gpio_number) {
  adc_unit_t adc_unit = ADC_UNIT_1;
  adc_channel_t adc_channel = ADC_CHANNEL_0;

  if (adc_oneshot_io_to_channel(gpio_number, &adc_unit, &adc_channel) !=
      ESP_OK) {
    ESP_LOGE(TAG, "\"sensor_pin\" %d is not an ADC pin", gpio_number);
    return false;
  }
  if (adc_unit != ADC_UNIT_1) {
    ESP_LOGE(TAG, "\"sensor_pin\" %d is on ADC2, which this device cannot use",
             gpio_number);
    return false;
  }
  return true;
}

/**
 * @brief Apply a positive integer member from the JSON object to the
 * destination variable.
 *
 * @param configuration The cJSON object containing the configuration.
 * @param member_name The name of the member to apply.
 * @param destination Pointer to the destination variable.
 */
static void apply_positive_integer_member(const cJSON *configuration,
                                          const char *member_name,
                                          int *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, member_name);
  if (member == NULL) {
    return;
  }
  if (!cJSON_IsNumber(member) || member->valueint <= 0) {
    ESP_LOGE(TAG, "\"%s\" must be a positive number, keeping %d", member_name,
             *destination);
    return;
  }
  *destination = member->valueint;
}

static void apply_integer_member(const cJSON *configuration,
                                 const char *member_name, int *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, member_name);
  if (member == NULL) {
    return;
  }
  if (!cJSON_IsNumber(member)) {
    ESP_LOGE(TAG, "\"%s\" must be a number, keeping %d", member_name,
             *destination);
    return;
  }
  *destination = member->valueint;
}

static void apply_float_member(const cJSON *configuration,
                               const char *member_name, float *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, member_name);
  if (member == NULL) {
    return;
  }
  if (!cJSON_IsNumber(member)) {
    ESP_LOGE(TAG, "\"%s\" must be a number", member_name);
    return;
  }
  *destination = (float)member->valuedouble;
}

/**
 * @brief Apply the sensor pin configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static void apply_sensor_pin(const cJSON *configuration) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, "sensor_pin");
  if (member == NULL) {
    return;
  }
  if (!cJSON_IsNumber(member)) {
    ESP_LOGE(TAG, "\"sensor_pin\" must be a number");
    return;
  }
  if (sensor_pin_is_usable(member->valueint)) {
    angle_sensor_config.sensor_gpio_number = member->valueint;
  }
}

/**
 * @brief Copy the sensor topic configuration from the JSON object into the
 * actual angle sensor configuration.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static void apply_topic(const cJSON *configuration) {
  const char *topic = cJSON_GetStringValue(
      cJSON_GetObjectItemCaseSensitive(configuration, "sensor_topic"));
  if (topic == NULL) {
    return;
  }
  if (topic[0] == '\0' ||
      strlen(topic) >= sizeof(angle_sensor_config.sensor_topic)) {
    ESP_LOGE(TAG,
             "\"sensor_topic\" must be non-empty and at most %d characters",
             (int)sizeof(angle_sensor_config.sensor_topic) - 1);
    return;
  }
  strlcpy(angle_sensor_config.sensor_topic, topic,
          sizeof(angle_sensor_config.sensor_topic));
}

/**
 * @brief Apply the angle sensor configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
void angle_sensor_config_apply_json(const cJSON *configuration) {
  if (configuration == NULL) {
    return;
  }

  apply_sensor_pin(configuration);
  apply_positive_integer_member(configuration, "sensor_sample_period_ms",
                                &angle_sensor_config.sensor_sample_period_ms);
  apply_positive_integer_member(
      configuration, "sensor_samples_per_reading",
      &angle_sensor_config.sensor_samples_per_reading);
  apply_integer_member(configuration, "sensor_minimum_millivolts",
                       &angle_sensor_config.sensor_minimum_millivolts);
  apply_integer_member(configuration, "sensor_maximum_millivolts",
                       &angle_sensor_config.sensor_maximum_millivolts);
  apply_float_member(configuration, "sensor_minimum_degrees",
                     &angle_sensor_config.sensor_minimum_degrees);
  apply_float_member(configuration, "sensor_maximum_degrees",
                     &angle_sensor_config.sensor_maximum_degrees);
  apply_float_member(configuration, "sensor_center_degrees",
                     &angle_sensor_config.sensor_center_degrees);
  apply_topic(configuration);

  ESP_LOGI(TAG,
           "Angle sensor configuration: pin=%d, sample period=%d ms, "
           "samples per reading=%d, topic='%s'",
           angle_sensor_config.sensor_gpio_number,
           angle_sensor_config.sensor_sample_period_ms,
           angle_sensor_config.sensor_samples_per_reading,
           angle_sensor_config.sensor_topic);
}
