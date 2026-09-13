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

static angle_sensor_config_t angle_sensor_config = {
    .sensor_gpio_number = -1,
    .sensor_sample_period_ms = 100,
    .sensor_samples_per_reading = 8,
    .sensor_topic = "sensors/rudders/starboard",
    .sensor_type = "elobau_424A11A040B",
};

static void apply_positive_integer_member(const cJSON *configuration,
                                          const char *member_name,
                                          int *destination);
static void apply_sensor_pin(const cJSON *configuration);
static void apply_topic(const cJSON *configuration);
static void apply_sensor_type(const cJSON *configuration);

const void *angle_sensor_config_get(void)
{
    return &angle_sensor_config;
}

size_t angle_sensor_config_size(void)
{
    return sizeof(angle_sensor_config);
}

bool angle_sensor_can_serve_type(const char *type)
{
    if (type == NULL)
    {
        return false;
    }

    for (size_t index = 0U;
         index < sizeof(ANGLE_SENSOR_CONFIG_TYPES) / sizeof(ANGLE_SENSOR_CONFIG_TYPES[0]);
         ++index)
    {
        if (strcmp(type, ANGLE_SENSOR_CONFIG_TYPES[index]) == 0)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Apply the entire angle sensor configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static void apply_configuration(const cJSON *configuration)
{
    apply_sensor_type(configuration);
    apply_sensor_pin(configuration);
    apply_positive_integer_member(configuration, "sensor_sample_period_ms",
                                  &angle_sensor_config.sensor_sample_period_ms);
    apply_positive_integer_member(configuration, "sensor_samples_per_reading",
                                  &angle_sensor_config.sensor_samples_per_reading);
    apply_topic(configuration);
}

static void apply_sensor_type(const cJSON *configuration)
{
    const char *type = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration, "type"));
    if (type != NULL)
    {
        strlcpy(angle_sensor_config.sensor_type, type,
                sizeof(angle_sensor_config.sensor_type));
    }
}

/**
 * @brief Configure the angle sensor with the given JSON configuration.
 *
 * @param configuration The cJSON object containing the configuration.
 * @return true if the configuration was successfully applied, false otherwise.
 */
bool angle_sensor_configure(const cJSON *configuration)
{
    const char *type = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration, "type"));

    if (!angle_sensor_can_serve_type(type))
    {
        return false;
    }
    /* validate the incoming configuration */
    const cJSON *sensor_pin = cJSON_GetObjectItemCaseSensitive(configuration, "sensor_pin");
    const cJSON *sample_period =
        cJSON_GetObjectItemCaseSensitive(configuration, "sensor_sample_period_ms");
    const cJSON *samples_per_reading =
        cJSON_GetObjectItemCaseSensitive(configuration, "sensor_samples_per_reading");
    const char *topic = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration, "sensor_topic"));

    if (!cJSON_IsNumber(sensor_pin) || !cJSON_IsNumber(sample_period) ||
        sample_period->valueint <= 0 || !cJSON_IsNumber(samples_per_reading) ||
        samples_per_reading->valueint <= 0 || topic == NULL || topic[0] == '\0' ||
        strlen(topic) >= ANGLE_SENSOR_CONFIG_TOPIC_SIZE)
    {
        ESP_LOGW(TAG, "configure_sensor requires sensor_pin, sensor_topic, "
                      "sensor_sample_period_ms and sensor_samples_per_reading");
        return false;
    }
    /* below this line the configuration is considered valid */
    apply_configuration(configuration);
    return true;
}

void angle_sensor_config_restore(const void *configuration)
{
    if (configuration != NULL)
    {
        memcpy(&angle_sensor_config, configuration, sizeof(angle_sensor_config));
    }
}

bool angle_sensor_config_add_json(json_gen_str_t *json, const void *configuration)
{
    const angle_sensor_config_t *config = configuration;
    return json != NULL && config != NULL &&
           json_gen_obj_set_int(json, "sensor_pin", config->sensor_gpio_number) == 0 &&
           json_gen_obj_set_int(json, "sensor_samples_per_reading", config->sensor_samples_per_reading) == 0 &&
           json_gen_obj_set_int(json, "sensor_sample_period_ms", config->sensor_sample_period_ms) == 0 &&
           json_obj_set_escaped_string(json, "sensor_topic", config->sensor_topic);
}

static bool sensor_pin_is_usable(int gpio_number)
{
    adc_unit_t adc_unit = ADC_UNIT_1;
    adc_channel_t adc_channel = ADC_CHANNEL_0;

    if (adc_oneshot_io_to_channel(gpio_number, &adc_unit, &adc_channel) != ESP_OK)
    {
        ESP_LOGE(TAG, "\"sensor_pin\" %d is not an ADC pin", gpio_number);
        return false;
    }
    if (adc_unit != ADC_UNIT_1)
    {
        ESP_LOGE(TAG, "\"sensor_pin\" %d is on ADC2, which this device cannot use",
                 gpio_number);
        return false;
    }
    return true;
}

/**
 * @brief Apply a positive integer member from the JSON object to the destination variable.
 *
 * @param configuration The cJSON object containing the configuration.
 * @param member_name The name of the member to apply.
 * @param destination Pointer to the destination variable.
 */
static void apply_positive_integer_member(const cJSON *configuration, const char *member_name,
                                          int *destination)
{
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(configuration, member_name);
    if (member == NULL)
    {
        return;
    }
    if (!cJSON_IsNumber(member) || member->valueint <= 0)
    {
        ESP_LOGE(TAG, "\"%s\" must be a positive number, keeping %d", member_name,
                 *destination);
        return;
    }
    *destination = member->valueint;
}

/**
 * @brief Apply the sensor pin configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static void apply_sensor_pin(const cJSON *configuration)
{
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(configuration, "sensor_pin");
    if (member == NULL)
    {
        return;
    }
    if (!cJSON_IsNumber(member))
    {
        ESP_LOGE(TAG, "\"sensor_pin\" must be a number");
        return;
    }
    if (sensor_pin_is_usable(member->valueint))
    {
        angle_sensor_config.sensor_gpio_number = member->valueint;
    }
}

/**
 * @brief Copy the sensor topic configuration from the JSON object into the actual angle sensor configuration.
 *
 * @param configuration The cJSON object containing the configuration.
 */
static void apply_topic(const cJSON *configuration)
{
    const char *topic = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration, "sensor_topic"));
    if (topic == NULL)
    {
        return;
    }
    if (topic[0] == '\0' || strlen(topic) >= sizeof(angle_sensor_config.sensor_topic))
    {
        ESP_LOGE(TAG, "\"sensor_topic\" must be non-empty and at most %d characters",
                 (int)sizeof(angle_sensor_config.sensor_topic) - 1);
        return;
    }
    strlcpy(angle_sensor_config.sensor_topic, topic, sizeof(angle_sensor_config.sensor_topic));
}

/**
 * @brief Apply the angle sensor configuration from the JSON object.
 *
 * @param configuration The cJSON object containing the configuration.
 */
void angle_sensor_config_apply_json(const cJSON *configuration)
{
    if (configuration == NULL)
    {
        return;
    }

    apply_sensor_pin(configuration);
    apply_positive_integer_member(configuration, "sensor_sample_period_ms",
                                  &angle_sensor_config.sensor_sample_period_ms);
    apply_positive_integer_member(configuration, "sensor_samples_per_reading",
                                  &angle_sensor_config.sensor_samples_per_reading);
    apply_topic(configuration);

    ESP_LOGI(TAG, "Angle sensor configuration: pin=%d, sample period=%d ms, "
                  "samples per reading=%d, topic='%s'",
             angle_sensor_config.sensor_gpio_number,
             angle_sensor_config.sensor_sample_period_ms,
             angle_sensor_config.sensor_samples_per_reading,
             angle_sensor_config.sensor_topic);
}
