#include "device_config.h"

#include <stdlib.h>
#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "device_config";

/* Defaults live in exactly one place. A pin of -1 means "not configured yet",
 * which is the only setting the device genuinely cannot invent for itself.
 * The voltage span is the usable range of ADC_ATTEN_DB_12 on the ESP32, and it
 * maps onto a symmetric rudder deflection with the midpoint at 0 degrees. */
static device_config_t device_config = {
    .sensor_gpio_number = -1,
    .sensor_minimum_millivolts = 0,
    .sensor_maximum_millivolts = 3100,
    .sensor_minimum_degrees = -45.0f,
    .sensor_maximum_degrees = 45.0f,
    .publish_deadband_degrees = 0.5f,
    .sensor_topic = "sensors/rudders/starboard",
};

const device_config_t *device_config_get(void)
{
    return &device_config;
}

/* ADC2 is unusable in practice on the ESP32, so only pins the oneshot driver
 * maps onto ADC1 are accepted. Asking the driver avoids maintaining a private
 * copy of the pin table. */
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

/* Reads an integer member, leaving the destination untouched when the member is
 * absent or not a number. */
static void apply_integer_member(const cJSON *configuration, const char *member_name,
                                 int *destination)
{
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(configuration, member_name);
    if (member == NULL)
    {
        return;
    }
    if (!cJSON_IsNumber(member))
    {
        ESP_LOGE(TAG, "\"%s\" must be a number, keeping %d", member_name, *destination);
        return;
    }

    *destination = member->valueint;
}

static void apply_float_member(const cJSON *configuration, const char *member_name,
                               float *destination)
{
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(configuration, member_name);
    if (member == NULL)
    {
        return;
    }
    if (!cJSON_IsNumber(member))
    {
        ESP_LOGE(TAG, "\"%s\" must be a number, keeping %g", member_name, *destination);
        return;
    }

    *destination = (float)member->valuedouble;
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

    if (!sensor_pin_is_usable(member->valueint))
    {
        /* Rejection is logged by the validator; the previous pin stays in
         * place so a bad document cannot silence a working sensor. */
        return;
    }

    device_config.sensor_gpio_number = member->valueint;
}

static void apply_sensor_topic(const cJSON *configuration)
{
    const char *topic = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration, "sensor_topic"));
    if (topic == NULL)
    {
        return;
    }

    if (topic[0] == '\0')
    {
        ESP_LOGE(TAG, "\"sensor_topic\" must not be empty, keeping '%s'",
                 device_config.sensor_topic);
        return;
    }
    if (strlen(topic) >= sizeof(device_config.sensor_topic))
    {
        ESP_LOGE(TAG, "\"sensor_topic\" is longer than %d characters, keeping '%s'",
                 (int)sizeof(device_config.sensor_topic) - 1, device_config.sensor_topic);
        return;
    }

    strlcpy(device_config.sensor_topic, topic, sizeof(device_config.sensor_topic));
}

void device_config_apply_json(const cJSON *configuration)
{
    if (configuration == NULL)
    {
        return;
    }

    apply_sensor_pin(configuration);
    apply_integer_member(configuration, "sensor_min_mv",
                         &device_config.sensor_minimum_millivolts);
    apply_integer_member(configuration, "sensor_max_mv",
                         &device_config.sensor_maximum_millivolts);
    apply_float_member(configuration, "sensor_min_deg",
                       &device_config.sensor_minimum_degrees);
    apply_float_member(configuration, "sensor_max_deg",
                       &device_config.sensor_maximum_degrees);
    apply_float_member(configuration, "publish_deadband_deg",
                       &device_config.publish_deadband_degrees);
    apply_sensor_topic(configuration);

    /* A zero-width voltage span would divide by zero when converting, so fall
     * back to the shipped default rather than trusting the document. */
    if (device_config.sensor_maximum_millivolts == device_config.sensor_minimum_millivolts)
    {
        ESP_LOGE(TAG, "\"sensor_min_mv\" and \"sensor_max_mv\" must differ; "
                      "restoring 0..3100 mV");
        device_config.sensor_minimum_millivolts = 0;
        device_config.sensor_maximum_millivolts = 3100;
    }

    /* A negative deadband would publish on every sample. */
    if (device_config.publish_deadband_degrees < 0.0f)
    {
        ESP_LOGE(TAG, "\"publish_deadband_deg\" must not be negative; restoring 0.5");
        device_config.publish_deadband_degrees = 0.5f;
    }

    ESP_LOGI(TAG, "Sensor settings: pin=%d, %d..%d mV -> %g..%g deg, "
                  "deadband=%g deg, topic='%s'",
             device_config.sensor_gpio_number,
             device_config.sensor_minimum_millivolts,
             device_config.sensor_maximum_millivolts,
             device_config.sensor_minimum_degrees,
             device_config.sensor_maximum_degrees,
             device_config.publish_deadband_degrees,
             device_config.sensor_topic);
}

esp_err_t device_config_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        /* A device that has never been configured has no namespace at all. */
        ESP_LOGI(TAG, "No stored configuration: %s", esp_err_to_name(err));
        return err;
    }

    /* Ask for the length first, so the buffer matches whatever the broker sent
     * rather than a guess. */
    size_t payload_length = 0;
    err = nvs_get_str(nvs_handle, CONFIGURATION_NVS_PAYLOAD_KEY, NULL, &payload_length);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "No stored configuration document: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    char *payload = malloc(payload_length);
    if (payload == NULL)
    {
        ESP_LOGE(TAG, "Out of memory reading %u byte configuration document",
                 (unsigned)payload_length);
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs_handle, CONFIGURATION_NVS_PAYLOAD_KEY, payload, &payload_length);
    nvs_close(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read stored configuration: %s", esp_err_to_name(err));
        free(payload);
        return err;
    }

    /* Only the sensor settings are replayed here. The clock is deliberately
     * left alone, because a stored timestamp is stale by definition, and an
     * OTA is not re-triggered from a remembered firmware URL. */
    cJSON *configuration = cJSON_Parse(payload);
    free(payload);
    if (configuration == NULL)
    {
        ESP_LOGE(TAG, "Stored configuration is not valid JSON");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Restoring settings from the stored configuration document");
    device_config_apply_json(configuration);
    cJSON_Delete(configuration);
    return ESP_OK;
}
