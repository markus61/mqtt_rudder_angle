#include "uptime_sensor.h"

#include <limits.h>
#include <string.h>

#include "uptime_sensor_config.h"
#include "json_utils.h"

static const char *const UPTIME_SENSOR_TYPES[] = {
    "dummy_uptime",
};

static uptime_sensor_config_t configuration = {
    .interval_seconds = 60,
    .sensor_topic = "sensors/uptime",
    .sensor_type = "dummy_uptime",
};

const uptime_sensor_config_t *uptime_sensor_config(void)
{
    return &configuration;
}

const void *uptime_sensor_config_get(void)
{
    return &configuration;
}

size_t uptime_sensor_config_size(void)
{
    return sizeof(configuration);
}

void uptime_sensor_config_restore(const void *record)
{
    if (record != NULL)
    {
        memcpy(&configuration, record, sizeof(configuration));
    }
}

bool uptime_sensor_config_add_json(json_gen_str_t *json, const void *record)
{
    const uptime_sensor_config_t *config = record;
    return json != NULL && config != NULL &&
           json_gen_obj_set_int(json, "interval", config->interval_seconds) == 0 &&
           json_obj_set_escaped_string(json, "sensor_topic", config->sensor_topic);
}

bool uptime_sensor_can_serve_type(const char *type)
{
    if (type == NULL)
    {
        return false;
    }

    for (size_t index = 0U;
         index < sizeof(UPTIME_SENSOR_TYPES) / sizeof(UPTIME_SENSOR_TYPES[0]);
         ++index)
    {
        if (strcmp(type, UPTIME_SENSOR_TYPES[index]) == 0)
        {
            return true;
        }
    }

    return false;
}

bool uptime_sensor_configure(const cJSON *configuration_json)
{
    if (configuration_json == NULL ||
        !uptime_sensor_can_serve_type(cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(configuration_json, "type"))))
    {
        return false;
    }

    const cJSON *interval =
        cJSON_GetObjectItemCaseSensitive(configuration_json, "interval");
    if (!cJSON_IsNumber(interval) || interval->valueint <= 0 ||
        interval->valueint > INT_MAX / 1000)
    {
        return false;
    }

    const char *topic = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(configuration_json, "sensor_topic"));
    if (topic != NULL && (topic[0] == '\0' ||
                          strlen(topic) >= sizeof(configuration.sensor_topic)))
    {
        return false;
    }

    configuration.interval_seconds = interval->valueint;
    if (topic != NULL)
    {
        strlcpy(configuration.sensor_topic, topic, sizeof(configuration.sensor_topic));
    }
    strlcpy(configuration.sensor_type,
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(configuration_json, "type")),
            sizeof(configuration.sensor_type));
    return true;
}
