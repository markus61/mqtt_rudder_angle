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
    "elobau_424A11A120B",
};

/**
 * @brief The default configuration for the Elobau angle sensor.
 */
static const angle_sensor_config_t default_angle_sensor_config = {
    .sensor_gpio_number = 32,
    .sensor_sample_period_ms = 100,
    .sensor_samples_per_reading = 8,
    .sensor_minimum_millivolts = 3300,
    .sensor_maximum_millivolts = 0,
    .sensor_deadband_millivolt = 15,
    .sensor_topic = "sensors/rudders/starboard",
    .sensor_type = "elobau_424A11A040B",
    .sensor_minimum_degrees = 0.0f,
    .sensor_maximum_degrees = 40.0f,
    .sensor_center_degrees = 20.0f,
};

static esp_err_t apply_positive_integer_member(const cJSON *configuration,
                                               const char *member_name,
                                               int *destination);
static esp_err_t apply_integer_member(const cJSON *configuration,
                                      const char *member_name,
                                      int *destination);
static esp_err_t apply_float_member(const cJSON *configuration,
                                    const char *member_name,
                                    float *destination);
static esp_err_t apply_sensor_pin(const cJSON *configuration,
                                  angle_sensor_config_t *destination);
static esp_err_t apply_topic(const cJSON *configuration,
                             angle_sensor_config_t *destination);
static esp_err_t apply_configuration_json(const cJSON *configuration,
                                          angle_sensor_config_t *destination);

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

bool angle_sensor_config_init(angle_sensor_config_t *configuration,
                              const char *type) {
  if (configuration == NULL || !angle_sensor_can_serve_type(type)) {
    return false;
  }
  *configuration = default_angle_sensor_config;
  strlcpy(configuration->sensor_type, type, sizeof(configuration->sensor_type));
  return true;
}

esp_err_t angle_sensor_config_apply_json(angle_sensor_config_t *configuration,
                                         const cJSON *json) {
  if (configuration == NULL || !cJSON_IsObject(json)) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *type =
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "type"));

  if (type != NULL && strcmp(type, configuration->sensor_type) != 0) {
    return ESP_ERR_INVALID_ARG;
  }

  /* Validate the complete overlay against a copy, then commit once. This keeps
   * a rejected MQTT action from changing any live setting. */
  angle_sensor_config_t candidate = *configuration;
  esp_err_t err = apply_configuration_json(json, &candidate);
  if (err != ESP_OK) {
    return err;
  }
  *configuration = candidate;
  return ESP_OK;
}

void angle_sensor_config_apply_calibration(angle_sensor_config_t *configuration,
                                           int calibration_min_millivolts,
                                           int calibration_max_millivolts,
                                           bool *minimum_replaced,
                                           bool *maximum_replaced) {
  const bool replace_minimum =
      configuration != NULL &&
      calibration_min_millivolts < configuration->sensor_minimum_millivolts;
  const bool replace_maximum =
      configuration != NULL &&
      calibration_max_millivolts > configuration->sensor_maximum_millivolts;

  if (replace_minimum) {
    configuration->sensor_minimum_millivolts = calibration_min_millivolts;
  }
  if (replace_maximum) {
    configuration->sensor_maximum_millivolts = calibration_max_millivolts;
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
static esp_err_t apply_positive_integer_member(const cJSON *configuration,
                                               const char *member_name,
                                               int *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, member_name);
  if (member == NULL) {
    return ESP_OK;
  }
  if (!cJSON_IsNumber(member) || member->valueint <= 0) {
    ESP_LOGE(TAG, "\"%s\" must be a positive number", member_name);
    return ESP_ERR_INVALID_ARG;
  }
  *destination = member->valueint;
  return ESP_OK;
}

static esp_err_t apply_integer_member(const cJSON *configuration,
                                      const char *member_name,
                                      int *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, member_name);
  if (member == NULL) {
    return ESP_OK;
  }
  if (!cJSON_IsNumber(member)) {
    ESP_LOGE(TAG, "\"%s\" must be a number", member_name);
    return ESP_ERR_INVALID_ARG;
  }
  *destination = member->valueint;
  return ESP_OK;
}

static esp_err_t apply_float_member(const cJSON *configuration,
                                    const char *member_name,
                                    float *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, member_name);
  if (member == NULL) {
    return ESP_OK;
  }
  if (!cJSON_IsNumber(member)) {
    ESP_LOGE(TAG, "\"%s\" must be a number", member_name);
    return ESP_ERR_INVALID_ARG;
  }
  *destination = (float)member->valuedouble;
  return ESP_OK;
}

/**
 * @brief Apply the sensor pin configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static esp_err_t apply_sensor_pin(const cJSON *configuration,
                                  angle_sensor_config_t *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, "sensor_pin");
  if (member == NULL) {
    return ESP_OK;
  }
  if (!cJSON_IsNumber(member)) {
    ESP_LOGE(TAG, "\"sensor_pin\" must be a number");
    return ESP_ERR_INVALID_ARG;
  }
  if (!sensor_pin_is_usable(member->valueint)) {
    return ESP_ERR_INVALID_ARG;
  }
  destination->sensor_gpio_number = member->valueint;
  return ESP_OK;
}

/**
 * @brief Copy the sensor topic configuration from the JSON object into the
 * actual angle sensor configuration.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static esp_err_t apply_topic(const cJSON *configuration,
                             angle_sensor_config_t *destination) {
  const cJSON *member =
      cJSON_GetObjectItemCaseSensitive(configuration, "sensor_topic");
  if (member == NULL) {
    return ESP_OK;
  }
  const char *topic = cJSON_GetStringValue(member);
  if (topic == NULL) {
    ESP_LOGE(TAG, "\"sensor_topic\" must be a string");
    return ESP_ERR_INVALID_ARG;
  }
  if (topic[0] == '\0' || strlen(topic) >= sizeof(destination->sensor_topic)) {
    ESP_LOGE(TAG,
             "\"sensor_topic\" must be non-empty and at most %d characters",
             (int)sizeof(destination->sensor_topic) - 1);
    return ESP_ERR_INVALID_ARG;
  }
  strlcpy(destination->sensor_topic, topic, sizeof(destination->sensor_topic));
  return ESP_OK;
}

/**
 * @brief Apply the angle sensor configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static esp_err_t apply_configuration_json(const cJSON *configuration,
                                          angle_sensor_config_t *destination) {
  if (configuration == NULL || destination == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = apply_sensor_pin(configuration, destination);
  if (err == ESP_OK)
    err =
        apply_positive_integer_member(configuration, "sensor_sample_period_ms",
                                      &destination->sensor_sample_period_ms);
  if (err == ESP_OK)
    err = apply_positive_integer_member(
        configuration, "sensor_samples_per_reading",
        &destination->sensor_samples_per_reading);
  if (err == ESP_OK)
    err = apply_integer_member(configuration, "sensor_minimum_millivolts",
                               &destination->sensor_minimum_millivolts);
  if (err == ESP_OK)
    err = apply_integer_member(configuration, "sensor_maximum_millivolts",
                               &destination->sensor_maximum_millivolts);
  if (err == ESP_OK)
    err = apply_integer_member(configuration, "sensor_deadband_millivolt",
                               &destination->sensor_deadband_millivolt);
  if (err == ESP_OK)
    err = apply_float_member(configuration, "sensor_minimum_degrees",
                             &destination->sensor_minimum_degrees);
  if (err == ESP_OK)
    err = apply_float_member(configuration, "sensor_maximum_degrees",
                             &destination->sensor_maximum_degrees);
  if (err == ESP_OK)
    err = apply_float_member(configuration, "sensor_center_degrees",
                             &destination->sensor_center_degrees);
  if (err == ESP_OK)
    err = apply_topic(configuration, destination);
  if (err != ESP_OK) {
    return err;
  }

  ESP_LOGI(
      TAG,
      "Angle sensor configuration: pin=%d, sample period=%d ms, "
      "samples per reading=%d, topic='%s', min mV=%d, max mV=%d, "
      "deadband mV=%d, min deg=%.2f, max deg=%.2f, center deg=%.2f",
      destination->sensor_gpio_number, destination->sensor_sample_period_ms,
      destination->sensor_samples_per_reading, destination->sensor_topic,
      destination->sensor_minimum_millivolts,
      destination->sensor_maximum_millivolts,
      destination->sensor_deadband_millivolt,
      destination->sensor_minimum_degrees, destination->sensor_maximum_degrees,
      destination->sensor_center_degrees);
  return ESP_OK;
}
