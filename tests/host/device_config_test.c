/* Verify that a device name remains exactly one safe MQTT topic level. */
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "device_utils.h"
#include "esp_err.h"
#include "nvs.h"

size_t strlcpy(char *destination, const char *source, size_t destination_size)
{
    const size_t source_size = strlen(source);
    if (destination_size != 0U)
    {
        const size_t copy_size = source_size < destination_size - 1U
                                     ? source_size
                                     : destination_size - 1U;
        memcpy(destination, source, copy_size);
        destination[copy_size] = '\0';
    }
    return source_size;
}

bool device_utils_clock_set(const cJSON *configuration)
{
    (void)configuration;
    return true;
}

void device_utils_apply_ota_update_request(const char *firmware_url)
{
    (void)firmware_url;
}

void device_utils_log_configuration_item(const cJSON *item)
{
    (void)item;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    (void)name;
    (void)mode;
    (void)handle;
    return ESP_FAIL;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data,
                       size_t *size)
{
    (void)handle;
    (void)key;
    (void)data;
    (void)size;
    return ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data,
                       size_t size)
{
    (void)handle;
    (void)key;
    (void)data;
    (void)size;
    return ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_FAIL;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

#include "../../main/device_config.c"

static bool validate_name(const char *name)
{
    cJSON *configuration = cJSON_CreateObject();
    assert(configuration != NULL);
    cJSON_AddStringToObject(configuration, "your_name", name);
    const bool valid = device_config_validate(configuration);
    cJSON_Delete(configuration);
    return valid;
}

int main(void)
{
    assert(validate_name("mant1s-01_A"));
    assert(strcmp(device_config_get()->name, "mant1s-01_A") == 0);

    const char *const rejected_names[] = {
        "", "device/name", "device+name", "device#name", "device name",
        "device.name", "device:name", "m\xC3\xA4ntis",
    };
    for (size_t i = 0; i < sizeof(rejected_names) / sizeof(rejected_names[0]);
         ++i)
    {
        assert(!validate_name(rejected_names[i]));
        assert(strcmp(device_config_get()->name, "mant1s-01_A") == 0);
    }

    char maximum_length_name[DEVICE_CONFIG_NAME_SIZE] = {0};
    memset(maximum_length_name, 'a', sizeof(maximum_length_name) - 1U);
    assert(validate_name(maximum_length_name));

    char too_long_name[DEVICE_CONFIG_NAME_SIZE + 1U] = {0};
    memset(too_long_name, 'a', sizeof(too_long_name) - 1U);
    assert(!validate_name(too_long_name));

    cJSON *configuration = cJSON_CreateObject();
    assert(configuration != NULL);
    cJSON_AddNumberToObject(configuration, "your_name", 1);
    assert(!device_config_validate(configuration));
    cJSON_Delete(configuration);
    return 0;
}
