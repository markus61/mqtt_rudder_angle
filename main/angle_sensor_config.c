#include "angle_sensor_config.h"

#include <stdbool.h>
#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "angle_sensor_config";

static angle_sensor_config_t angle_sensor_config = {
    .sensor_gpio_number = -1,
    .sensor_sample_period_ms = 100,
    .sensor_samples_per_reading = 8,
    .sensor_topic = "sensors/rudders/starboard",
};

const angle_sensor_config_t *angle_sensor_config_get(void)
{
    return &angle_sensor_config;
}

void angle_sensor_config_restore(const angle_sensor_config_t *configuration)
{
    if (configuration != NULL)
    {
        angle_sensor_config = *configuration;
    }
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

    ESP_LOGI(TAG, "Angle sensor settings: pin=%d, sample period=%d ms, "
                  "samples per reading=%d, topic='%s'",
             angle_sensor_config.sensor_gpio_number,
             angle_sensor_config.sensor_sample_period_ms,
             angle_sensor_config.sensor_samples_per_reading,
             angle_sensor_config.sensor_topic);
}
