#include "angle_sensor.h"

#include <math.h>

#include "device_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "init_mqtt.h"

static const char *TAG = "angle_sensor";

/* 10 Hz. The FreeRTOS tick is 10 ms, so this is an exact number of ticks and
 * the delay also keeps the task watchdog satisfied. */
#define SAMPLE_PERIOD_MS 100
/* ADC1 on the ESP32 is noisy enough that a single reading jitters by more than
 * the publish deadband, so each sample is the mean of a short burst. */
#define SAMPLES_PER_READING 8

/* All of the following are touched only from angle_sensor_task, which is a
 * single task, so no locking is needed. */
static adc_oneshot_unit_handle_t adc_unit_handle;
static adc_cali_handle_t adc_calibration_handle;
static adc_channel_t sensor_adc_channel;
/* Most recent angle, kept so the current reading is always available. */
static float last_angle_degrees;
/* Angle at the last publish. NAN until the first one, which makes the first
 * reading always exceed the deadband and therefore always publish. */
static float last_published_angle_degrees = NAN;

static TaskHandle_t angle_sensor_task_handle;

/* Averages a burst of raw readings and converts the result to millivolts. */
static bool read_sensor_millivolts(int *out_millivolts)
{
    int raw_total = 0;

    for (int sample_index = 0; sample_index < SAMPLES_PER_READING; sample_index++)
    {
        int raw_reading = 0;
        esp_err_t err = adc_oneshot_read(adc_unit_handle, sensor_adc_channel, &raw_reading);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(err));
            return false;
        }
        raw_total += raw_reading;
    }

    const int averaged_raw_reading = raw_total / SAMPLES_PER_READING;

    esp_err_t err = adc_cali_raw_to_voltage(adc_calibration_handle, averaged_raw_reading,
                                            out_millivolts);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "ADC calibration failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

/* Maps a sensor voltage onto the configured degree span, clamped to its ends so
 * a sensor that reads slightly outside its nominal range cannot report an
 * impossible deflection. */
static float convert_millivolts_to_degrees(int millivolts, const device_config_t *config)
{
    const float voltage_span = (float)(config->sensor_maximum_millivolts -
                                       config->sensor_minimum_millivolts);
    const float degree_span = config->sensor_maximum_degrees - config->sensor_minimum_degrees;

    const float position_in_span = (float)(millivolts - config->sensor_minimum_millivolts) /
                                   voltage_span;
    const float degrees = config->sensor_minimum_degrees + position_in_span * degree_span;

    /* The span may be configured in either direction, so clamp against the
     * lower and higher of the two ends rather than assuming min < max. */
    const float lower_limit = fminf(config->sensor_minimum_degrees,
                                    config->sensor_maximum_degrees);
    const float upper_limit = fmaxf(config->sensor_minimum_degrees,
                                    config->sensor_maximum_degrees);
    return fminf(fmaxf(degrees, lower_limit), upper_limit);
}

static void angle_sensor_task(void *task_argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true)
    {
        /* Re-read the configuration every iteration so a conversion span or
         * topic that arrives later takes effect without a restart. */
        const device_config_t *config = device_config_get();

        int millivolts = 0;
        if (read_sensor_millivolts(&millivolts))
        {
            last_angle_degrees = convert_millivolts_to_degrees(millivolts, config);

            /* isnan covers the very first reading, where there is nothing to
             * compare against yet. */
            const bool angle_changed =
                isnan(last_published_angle_degrees) ||
                fabsf(last_angle_degrees - last_published_angle_degrees) >=
                    config->publish_deadband_degrees;

            if (angle_changed &&
                mqtt_publish_sensor_reading(config->sensor_topic, last_angle_degrees))
            {
                /* Only advance the reference once the reading actually went
                 * out, so a publish refused while the broker is unreachable is
                 * retried on the next sample. */
                last_published_angle_degrees = last_angle_degrees;
            }
        }

        /* Delay against the last wake-up rather than "now", so the conversion
         * and publish time does not accumulate into a slower sample rate. */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

/* Characterises the ADC so raw counts can be turned into millivolts. On the
 * ESP32 the line fitting scheme always succeeds: without the eFuse calibration
 * bits it falls back to the nominal reference voltage, which is less accurate
 * but still usable. */
static esp_err_t create_adc_calibration(void)
{
#if CONFIG_IDF_TARGET_ESP32
    adc_cali_line_fitting_efuse_val_t efuse_calibration_value;
    if (adc_cali_scheme_line_fitting_check_efuse(&efuse_calibration_value) == ESP_OK &&
        efuse_calibration_value == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF)
    {
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

    return adc_cali_create_scheme_line_fitting(&calibration_config, &adc_calibration_handle);
}

esp_err_t angle_sensor_start(void)
{
    if (angle_sensor_task_handle != NULL)
    {
        ESP_LOGI(TAG, "Angle sensor is already running");
        return ESP_OK;
    }

    const device_config_t *config = device_config_get();
    if (config->sensor_gpio_number < 0)
    {
        ESP_LOGI(TAG, "No sensor pin configured yet, not sampling");
        return ESP_ERR_INVALID_STATE;
    }

    /* The pin was validated as an ADC1 pin when the configuration was applied,
     * so this only needs the channel it maps onto. */
    adc_unit_t resolved_adc_unit = ADC_UNIT_1;
    esp_err_t err = adc_oneshot_io_to_channel(config->sensor_gpio_number,
                                              &resolved_adc_unit, &sensor_adc_channel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO %d is not an ADC pin: %s",
                 config->sensor_gpio_number, esp_err_to_name(err));
        return err;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    err = adc_oneshot_new_unit(&unit_config, &adc_unit_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialise ADC1: %s", esp_err_to_name(err));
        return err;
    }

    /* 12 dB attenuation gives the widest input range, which suits a sensor
     * swinging across the full supply. */
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(adc_unit_handle, sensor_adc_channel, &channel_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(err));
        goto release_adc_unit;
    }

    err = create_adc_calibration();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to characterise the ADC: %s", esp_err_to_name(err));
        goto release_adc_unit;
    }

    if (xTaskCreate(angle_sensor_task, "angle_sensor", 4096, NULL, 5,
                    &angle_sensor_task_handle) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create the angle sensor task");
        err = ESP_ERR_NO_MEM;
        goto release_adc_calibration;
    }

    ESP_LOGI(TAG, "Sampling GPIO %d at %d Hz, publishing to '%s'",
             config->sensor_gpio_number, 1000 / SAMPLE_PERIOD_MS, config->sensor_topic);
    return ESP_OK;

release_adc_calibration:
    adc_cali_delete_scheme_line_fitting(adc_calibration_handle);
    adc_calibration_handle = NULL;
release_adc_unit:
    adc_oneshot_del_unit(adc_unit_handle);
    adc_unit_handle = NULL;
    return err;
}

void angle_sensor_calibrate(void)
{
    ESP_LOGI(TAG, "Angle sensor calibration requested");
}
