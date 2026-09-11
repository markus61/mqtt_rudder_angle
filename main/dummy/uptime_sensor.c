#include "uptime_sensor.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "init_mqtt.h"
#include "uptime_sensor_config.h"

static TaskHandle_t uptime_sensor_task_handle;

static void uptime_sensor_task(void *task_argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true)
    {
        const uptime_sensor_config_t *configuration = uptime_sensor_config();
        const float uptime_seconds = (float)esp_timer_get_time() / 1000000.0f;
        (void)mqtt_publish_uptime_reading(configuration->sensor_topic, uptime_seconds);

        vTaskDelayUntil(&last_wake_time,
                        pdMS_TO_TICKS((uint32_t)configuration->interval_seconds * 1000U));
    }
}

esp_err_t uptime_sensor_start(void)
{
    if (uptime_sensor_task_handle != NULL)
    {
        return ESP_OK;
    }

    if (xTaskCreate(uptime_sensor_task, "uptime_sensor", 3072, NULL, 5,
                    &uptime_sensor_task_handle) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
