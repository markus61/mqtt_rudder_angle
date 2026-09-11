#include "sensor_config.h"

#include <string.h>

#include "esp_log.h"
#include "elobau_angle_sensor.h"
#include "elobau_angle_sensor_config.h"
#include "registry_read_write.h"

static const char *TAG = "sensor_config";

static registry_t *active_lookup;

#define ATTACHED_FEATURE_COUNT 1U

static feature_entry_t angle_sensor_lookup_config = {
    .name = "starboard_rudder_angel",
    .type = "angle",
};
static feature_entry_t *configuration_storage[ATTACHED_FEATURE_COUNT];
static feature_entry_t *match_storage[ATTACHED_FEATURE_COUNT];
static registry_t boot_lookup;

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

bool sensor_config_lookup_init(registry_t *lookup,
                               feature_entry_t *configuration_storage[],
                               size_t configuration_capacity,
                               feature_entry_t *match_storage[], size_t match_capacity)
{
    if (lookup == NULL || configuration_storage == NULL || match_storage == NULL ||
        configuration_capacity == 0U || match_capacity < configuration_capacity)
    {
        return false;
    }

    lookup->configurations = configuration_storage;
    lookup->capacity = configuration_capacity;
    lookup->count = 0U;
    lookup->matches = match_storage;
    lookup->match_capacity = match_capacity;
    active_lookup = lookup;
    return true;
}

bool sensor_config_lookup_add(registry_t *lookup, feature_entry_t *configuration)
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

size_t sensor_json_dump(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 3U || active_lookup == NULL ||
        active_lookup->configurations == NULL)
    {
        return 0U;
    }

    size_t length = 0U;
    buffer[length++] = '[';

    for (size_t index = 0U; index < active_lookup->count; ++index)
    {
        const feature_entry_t *configuration = active_lookup->configurations[index];
        if (configuration == NULL || configuration->type == NULL)
        {
            return 0U;
        }

        if (index != 0U)
        {
            if (length + 2U > buffer_size)
            {
                return 0U;
            }
            buffer[length++] = ',';
        }

        size_t configuration_length = 0U;
        if (strcmp(configuration->type, "angle") == 0)
        {
            configuration_length = angle_sensor_config_format_feature_json(
                buffer + length, buffer_size - length - 1U);
        }

        if (configuration_length == 0U)
        {
            return 0U;
        }
        length += configuration_length;
    }

    if (length + 2U > buffer_size)
    {
        return 0U;
    }
    buffer[length++] = ']';
    buffer[length] = '\0';
    return length;
}

esp_err_t registry_init_on_boot(void)
{
    if (active_lookup == NULL)
    {
        if (!sensor_config_lookup_init(&boot_lookup, configuration_storage,
                                       ATTACHED_FEATURE_COUNT, match_storage,
                                       ATTACHED_FEATURE_COUNT))
        {
            return ESP_ERR_INVALID_STATE;
        }

        angle_sensor_lookup_config.configuration = (void *)angle_sensor_config_get();
        angle_sensor_lookup_config.settings_size = sizeof(angle_sensor_config_t);
        if (!sensor_config_lookup_add(active_lookup, &angle_sensor_lookup_config))
        {
            active_lookup = NULL;
            return ESP_ERR_INVALID_STATE;
        }
    }
    return registry_read(active_lookup);
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

    feature_entry_t configuration = {
        .name = name,
        .type = type,
    };

    switch (type[0])
    {
    case 'e':
        if (strcmp(type, "elobau_424A11A040B") != 0 &&
            strcmp(type, "elobau_424A11A060B") != 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
        configuration.configuration = (void *)angle_sensor_config_get();
        configuration.settings_size = sizeof(angle_sensor_config_t);
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
        angle_sensor_lookup_config.configuration = configuration.configuration;
        angle_sensor_lookup_config.settings_size = configuration.settings_size;
        break;

    default:
        ESP_LOGW(TAG, "Unsupported sensor type '%s'", type);
        return ESP_ERR_INVALID_ARG;
    }

    /* The sensor entry is registered during boot and owns the persistent storage. */
    if (active_lookup == NULL ||
        sensor_config_lookup_by_name(active_lookup, angle_sensor_lookup_config.name).count == 0U)
    {
        if (!sensor_config_lookup_add(active_lookup, &angle_sensor_lookup_config))
        {
            return ESP_ERR_INVALID_STATE;
        }
    }

    return registry_write(active_lookup);
}
