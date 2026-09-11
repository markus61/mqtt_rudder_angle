#include "handle_control_action.h"

#include <stddef.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sensor_config.h"
#include "init_mqtt.h"

static const char *TAG = "control_action";

typedef enum
{
    CMD_UNKNOWN = -1,
    CMD_BRAINDUMP,
    CMD_CONFIGURE_FEATURE,
    CMD_RESET
} control_action_t;

static control_action_t parse_action(const char *action_string)
{
    if (strcasecmp(action_string, "braindump") == 0)
        return CMD_BRAINDUMP;
    if (strcasecmp(action_string, "configure_feature") == 0)
        return CMD_CONFIGURE_FEATURE;
    if (strcasecmp(action_string, "reset") == 0)
        return CMD_RESET;
    return CMD_UNKNOWN;
}

void handle_control_action(const char *payload, int payload_length)
{
    cJSON *action_json = cJSON_ParseWithLength(payload, (size_t)payload_length);
    if (!cJSON_IsObject(action_json))
    {
        ESP_LOGW(TAG, "Control action must be a JSON object");
        cJSON_Delete(action_json);
        return;
    }

    const cJSON *name = cJSON_GetObjectItemCaseSensitive(action_json, "action");
    if (!cJSON_IsString(name) || name->valuestring == NULL)
    {
        ESP_LOGW(TAG, "Control action is missing a string \"action\" member");
        cJSON_Delete(action_json);
        return;
    }

    switch (parse_action(name->valuestring))
    {
    case CMD_BRAINDUMP:
        ESP_LOGI(TAG, "Handling control action 'braindump'");
        mqtt_publish_braindump();
        break;
    case CMD_CONFIGURE_FEATURE:
    {
        ESP_LOGI(TAG, "Handling control action 'configure_feature'");
        const esp_err_t err = sensor_config_from_mqtt(action_json);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Could not configure feature: %s", esp_err_to_name(err));
        }
        break;
    }
    case CMD_RESET:
        ESP_LOGI(TAG, "Handling control action 'reset'");
        cJSON_Delete(action_json);
        esp_restart();
        return;
    default:
        ESP_LOGW(TAG, "Unknown control action '%s'", name->valuestring);
        break;
    }

    cJSON_Delete(action_json);
}
