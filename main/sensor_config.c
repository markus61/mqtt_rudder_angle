/**
 * @file sensor_config.c
 * @brief Implementation of sensor configuration management.
 */
#include "sensor_config.h"

#include <limits.h>
#include <string.h>

#include "esp_log.h"
#include "elobau_angle_sensor.h"
#include "elobau_angle_sensor_config.h"
#include "uptime_sensor.h"
#include "uptime_sensor_config.h"
#include "json_utils.h"
#include "registry_read_write.h"
#include "json_generator.h"

static const char *TAG = "sensor_config";

static registry_t *active_lookup;

static feature_entry_t angle_sensor_lookup_config = {
    .name = "starboard_rudder_angle",
    .type = "angle",
};

static registry_lookup_result_t empty_result(registry_t *lookup)
{
    registry_lookup_result_t result = {
        .items = lookup != NULL ? lookup->matches : NULL,
        .count = 0U,
    };
    return result;
}

static bool valid_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static bool append_feature_configuration_json(json_gen_str_t *generator,
                                              const feature_entry_t *feature)
{
    if (strcmp(feature->type, "angle") != 0 || feature->configuration == NULL ||
        feature->configuration_size != sizeof(angle_sensor_config_t))
    {
        return false;
    }

    const angle_sensor_config_t *config = feature->configuration;
    return json_obj_set_escaped_string(generator, "type", config->sensor_type) &&
           json_gen_obj_set_int(generator, "sensor_pin", config->sensor_gpio_number) == 0 &&
           json_gen_obj_set_int(generator, "sensor_samples_per_reading",
                                config->sensor_samples_per_reading) == 0 &&
           json_gen_obj_set_int(generator, "sensor_sample_period_ms",
                                config->sensor_sample_period_ms) == 0 &&
           json_obj_set_escaped_string(generator, "sensor_topic", config->sensor_topic);
}

bool registry_feature_add(registry_t *lookup, feature_entry_t *configuration)
{
    if (lookup == NULL || lookup->configurations == NULL || lookup->matches == NULL ||
        configuration == NULL || !valid_text(configuration->name) ||
        !valid_text(configuration->type) || lookup->count >= lookup->capacity)
    {
        return false;
    }

    for (size_t index = 0U; index < lookup->count; ++index)
    {
        if (strcmp(lookup->configurations[index]->name, configuration->name) == 0)
        {
            return false;
        }
    }

    lookup->configurations[lookup->count++] = configuration;
    return true;
}

registry_lookup_result_t
sensor_config_lookup_by_name(registry_t *lookup, const char *name)
{
    registry_lookup_result_t result = empty_result(lookup);
    if (lookup == NULL || lookup->configurations == NULL || lookup->matches == NULL ||
        !valid_text(name))
    {
        return result;
    }

    for (size_t index = 0U; index < lookup->count; ++index)
    {
        feature_entry_t *configuration = lookup->configurations[index];
        if (strcmp(configuration->name, name) == 0)
        {
            lookup->matches[0] = configuration;
            result.items = lookup->matches;
            result.count = 1U;
            return result;
        }
    }

    return result;
}

registry_lookup_result_t
sensor_config_lookup_by_type(registry_t *lookup, const char *type)
{
    registry_lookup_result_t result = empty_result(lookup);
    if (lookup == NULL || lookup->configurations == NULL || lookup->matches == NULL ||
        !valid_text(type))
    {
        return result;
    }

    for (size_t index = 0U; index < lookup->count; ++index)
    {
        feature_entry_t *configuration = lookup->configurations[index];
        if (strcmp(configuration->type, type) == 0)
        {
            if (result.count == lookup->match_capacity)
            {
                return empty_result(lookup);
            }
            lookup->matches[result.count++] = configuration;
        }
    }

    result.items = lookup->matches;
    return result;
}

size_t registry_features_json_dump(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 3U || active_lookup == NULL ||
        active_lookup->configurations == NULL || buffer_size > INT_MAX)
    {
        return 0U;
    }

    json_gen_str_t generator;
    json_gen_str_start(&generator, buffer, (int)buffer_size, NULL, NULL);
    if (json_gen_start_array(&generator) != 0)
    {
        return 0U;
    }

    for (size_t index = 0U; index < active_lookup->count; ++index)
    {
        const feature_entry_t *configuration = active_lookup->configurations[index];
        if (configuration == NULL || !valid_text(configuration->name) ||
            !valid_text(configuration->type))
        {
            return 0U;
        }

        if (json_gen_start_object(&generator) != 0 ||
            !json_obj_set_escaped_string(&generator, "name", configuration->name))
        {
            return 0U;
        }

        if (!append_feature_configuration_json(&generator, configuration))
        {
            return 0U;
        }

        if (json_gen_end_object(&generator) != 0)
        {
            return 0U;
        }
    }

    if (json_gen_end_array(&generator) != 0)
    {
        return 0U;
    }
    const int length = json_gen_str_end(&generator);
    return length <= 1 || (size_t)length > buffer_size ? 0U : (size_t)length - 1U;
}

esp_err_t registry_init_on_boot(void)
{
    active_lookup = registry_init();
    return active_lookup == NULL ? ESP_ERR_INVALID_STATE : ESP_OK;
}

registry_t *registry_active_lookup(void)
{
    return active_lookup;
}

esp_err_t sensor_config_from_mqtt(const cJSON *action_json)
{
    const char *type = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(action_json, "type"));
    const char *name = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(action_json, "name"));
    if (!valid_text(type) || !valid_text(name))
    {
        ESP_LOGW(TAG, "configure_feature requires valid 'name' and 'type' values");
        return ESP_ERR_INVALID_ARG;
    }

    feature_entry_t entry = {
        .name = name,
        .type = type,
    };

    if (angle_sensor_can_serve_type(type))
    {
        entry.configuration = (void *)angle_sensor_config_get();
        entry.configuration_size = angle_sensor_config_size();
        ESP_LOGI(TAG, "Configuring angle sensor for type '%s'", type);
        if (!angle_sensor_configure(action_json))
        {
            return ESP_ERR_INVALID_ARG;
        }
        if (angle_sensor_start() != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to start angle sensor, reverting to stored configuration");
            (void)registry_read(active_lookup);
            return ESP_ERR_INVALID_STATE;
        }
        if (!registry_feature_add(active_lookup, &entry))
        {
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (uptime_sensor_can_serve_type(type))
    {
        entry.configuration = (void *)uptime_sensor_config_get();
        entry.configuration_size = uptime_sensor_config_size();
        ESP_LOGI(TAG, "Configuring uptime sensor for type '%s'", type);
        if (!uptime_sensor_configure(action_json))
        {
            return ESP_ERR_INVALID_ARG;
        }
        if (uptime_sensor_start() != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to start uptime sensor, reverting to stored configuration");
            (void)registry_read(active_lookup);
            return ESP_ERR_INVALID_STATE;
        }
        if (!registry_feature_add(active_lookup, &entry))
        {
            return ESP_ERR_INVALID_STATE;
        }
    }

    return registry_write(active_lookup);
}
