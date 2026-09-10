#include "device_utils.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "device_utils";

/* Parses an RFC3339 timestamp such as "2026-09-06T12:34:56.789+02:00" into
 * seconds and microseconds since the epoch. Returns false on anything that
 * does not match the grammar, because a half-parsed timestamp would silently
 * set the clock to a wrong value. */
static bool parse_rfc3339(const char *timestamp, struct timeval *utc_time)
{
    struct tm broken_down_time = {0};
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int consumed_characters = 0;

    if (sscanf(timestamp, "%4d-%2d-%2dT%2d:%2d:%2d%n",
               &year, &month, &day, &hour, &minute, &second, &consumed_characters) != 6)
    {
        return false;
    }

    const char *remainder = timestamp + consumed_characters;

    /* Optional fractional seconds, converted to whole microseconds. */
    long microseconds = 0;
    if (*remainder == '.' || *remainder == ',')
    {
        remainder++;
        if (*remainder < '0' || *remainder > '9')
        {
            return false;
        }
        long scale = 100000;
        while (*remainder >= '0' && *remainder <= '9')
        {
            if (scale > 0)
            {
                microseconds += (*remainder - '0') * scale;
                scale /= 10;
            }
            remainder++;
        }
    }

    /* Mandatory offset: "Z" or "+hh:mm" / "-hh:mm". */
    long offset_seconds = 0;
    if (*remainder == 'Z' || *remainder == 'z')
    {
        remainder++;
    }
    else if (*remainder == '+' || *remainder == '-')
    {
        const int offset_sign = (*remainder == '-') ? -1 : 1;
        int offset_hours = 0;
        int offset_minutes = 0;
        if (sscanf(remainder + 1, "%2d:%2d%n", &offset_hours, &offset_minutes,
                   &consumed_characters) != 2)
        {
            return false;
        }
        if (offset_hours > 23 || offset_minutes > 59)
        {
            return false;
        }
        offset_seconds = offset_sign * (offset_hours * 3600L + offset_minutes * 60L);
        remainder += 1 + consumed_characters;
    }
    else
    {
        return false;
    }

    if (*remainder != '\0')
    {
        return false;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 60)
    {
        return false;
    }

    broken_down_time.tm_year = year - 1900;
    broken_down_time.tm_mon = month - 1;
    broken_down_time.tm_mday = day;
    broken_down_time.tm_hour = hour;
    broken_down_time.tm_min = minute;
    broken_down_time.tm_sec = second;

    /* timegm() interprets the fields as UTC, so the offset is subtracted
     * afterwards to get back to true UTC. */
    const time_t seconds_since_epoch = timegm(&broken_down_time);
    if (seconds_since_epoch == (time_t)-1)
    {
        return false;
    }

    utc_time->tv_sec = seconds_since_epoch - offset_seconds;
    utc_time->tv_usec = (suseconds_t)microseconds;
    return true;
}

/* Reads the "now" member and applies it to the system clock. */
bool device_utils_clock_set(const cJSON *configuration)
{
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(configuration, "now");
    if (!cJSON_IsString(now) || now->valuestring == NULL)
    {
        ESP_LOGE(TAG, "Configuration is missing a string \"now\" member");
        return false;
    }

    struct timeval utc_time = {0};
    if (!parse_rfc3339(now->valuestring, &utc_time))
    {
        ESP_LOGE(TAG, "\"now\" is not a valid RFC3339 timestamp: \"%s\"", now->valuestring);
        return false;
    }

    if (settimeofday(&utc_time, NULL) != 0)
    {
        ESP_LOGE(TAG, "Failed to set the system clock from \"%s\"", now->valuestring);
        return false;
    }

    char readable_time[32] = "";
    struct tm utc_broken_down_time = {0};
    gmtime_r(&utc_time.tv_sec, &utc_broken_down_time);
    strftime(readable_time, sizeof(readable_time), "%Y-%m-%dT%H:%M:%SZ", &utc_broken_down_time);
    ESP_LOGI(TAG, "Device clock set to %s", readable_time);
    return true;
}

void device_utils_apply_ota_update_request(const char *firmware_url)
{
    const esp_partition_t *target_partition = esp_ota_get_next_update_partition(NULL);
    if (firmware_url != NULL)
    {
        if (target_partition == NULL)
        {
            ESP_LOGE(TAG, "OTA update requested but no target OTA partition is available");
            return;
        }

        ESP_LOGI(TAG, "OTA update requested by configuration. Target Partition: %s", target_partition->label);
        ESP_LOGI(TAG, "Firmware URL: %s", firmware_url);

        esp_http_client_config_t http_config = {
            .url = firmware_url,
            .timeout_ms = 30000,
            .keep_alive_enable = true,
        };
        esp_https_ota_config_t ota_config = {
            .http_config = &http_config,
            .partition.staging = target_partition,
        };

        esp_err_t err = esp_https_ota(&ota_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
            return;
        }

        ESP_LOGI(TAG, "OTA update completed successfully; restarting");
        esp_restart();
    }
}

/* Logs one JSON member, so the parsed result is visible without knowing the
 * schema yet. */
void device_utils_log_configuration_item(const cJSON *item)
{
    if (cJSON_IsString(item))
    {
        ESP_LOGI(TAG, "  %s = \"%s\"", item->string, item->valuestring);
    }
    else if (cJSON_IsNumber(item))
    {
        ESP_LOGI(TAG, "  %s = %g", item->string, item->valuedouble);
    }
    else if (cJSON_IsBool(item))
    {
        ESP_LOGI(TAG, "  %s = %s", item->string, cJSON_IsTrue(item) ? "true" : "false");
    }
    else if (cJSON_IsNull(item))
    {
        ESP_LOGI(TAG, "  %s = null", item->string);
    }
    else
    {
        /* Nested object or array; log it verbatim rather than recursing. */
        char *nested_json = cJSON_PrintUnformatted(item);
        ESP_LOGI(TAG, "  %s = %s", item->string, nested_json ? nested_json : "<unprintable>");
        cJSON_free(nested_json);
    }
}
