/**
 * @file registry_read_write.c
 * @brief Functions for reading and writing the sensor configuration registry to NVS.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "device_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sensor_config.h"
#include "registry_read_write.h"

#define SENSOR_CONFIG_LOOKUP_NVS_KEY "sensor_configs"
#define SENSOR_CONFIG_LOOKUP_NVS_MAGIC UINT32_C(0x53434647)
#define SENSOR_CONFIG_LOOKUP_NVS_VERSION UINT16_C(1)
#define REGISTRY_INITIAL_CAPACITY 10U

/* The registry owns only its pointer indexes; individual sensors own their
 * configuration records.  Keeping this here lets the registry grow without
 * teaching its initialization about any sensor implementation. */
static feature_entry_t *configuration_storage[REGISTRY_INITIAL_CAPACITY];
static feature_entry_t *match_storage[REGISTRY_INITIAL_CAPACITY];
static registry_t registry;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
} sensor_config_nvs_header_t;

typedef struct
{
    uint32_t name_size;
    uint32_t type_size;
    uint32_t settings_size;
} sensor_config_nvs_record_t;

static bool valid_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static bool size_add(size_t *total, size_t additional)
{
    if (additional > SIZE_MAX - *total)
    {
        return false;
    }
    *total += additional;
    return true;
}

esp_err_t registry_write(const registry_t *lookup)
{
    if (lookup == NULL || lookup->configurations == NULL || lookup->count == 0U ||
        lookup->count > UINT16_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t serialized_size = sizeof(sensor_config_nvs_header_t);
    for (size_t index = 0U; index < lookup->count; ++index)
    {
        const feature_entry_t *configuration = lookup->configurations[index];
        if (configuration == NULL || !valid_text(configuration->name) ||
            !valid_text(configuration->type) || configuration->configuration == NULL ||
            configuration->configuration_size == 0U || strlen(configuration->name) > UINT32_MAX ||
            strlen(configuration->type) > UINT32_MAX || configuration->configuration_size > UINT32_MAX ||
            !size_add(&serialized_size, sizeof(sensor_config_nvs_record_t)) ||
            !size_add(&serialized_size, strlen(configuration->name)) ||
            !size_add(&serialized_size, strlen(configuration->type)) ||
            !size_add(&serialized_size, configuration->configuration_size))
        {
            return ESP_ERR_INVALID_ARG;
        }
    }

    uint8_t *serialized = malloc(serialized_size);
    if (serialized == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    sensor_config_nvs_header_t header = {
        .magic = SENSOR_CONFIG_LOOKUP_NVS_MAGIC,
        .version = SENSOR_CONFIG_LOOKUP_NVS_VERSION,
        .count = (uint16_t)lookup->count,
    };
    uint8_t *cursor = serialized;
    memcpy(cursor, &header, sizeof(header));
    cursor += sizeof(header);

    for (size_t index = 0U; index < lookup->count; ++index)
    {
        const feature_entry_t *configuration = lookup->configurations[index];
        const size_t name_size = strlen(configuration->name);
        const size_t type_size = strlen(configuration->type);
        sensor_config_nvs_record_t record = {
            .name_size = (uint32_t)name_size,
            .type_size = (uint32_t)type_size,
            .settings_size = (uint32_t)configuration->configuration_size,
        };
        memcpy(cursor, &record, sizeof(record));
        cursor += sizeof(record);
        memcpy(cursor, configuration->name, name_size);
        cursor += name_size;
        memcpy(cursor, configuration->type, type_size);
        cursor += type_size;
        memcpy(cursor, configuration->configuration, configuration->configuration_size);
        cursor += configuration->configuration_size;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK)
    {
        err = nvs_set_blob(nvs_handle, SENSOR_CONFIG_LOOKUP_NVS_KEY, serialized,
                           serialized_size);
        if (err == ESP_OK)
        {
            err = nvs_commit(nvs_handle);
        }
        nvs_close(nvs_handle);
    }
    free(serialized);
    return err;
}

static feature_entry_t *find_configuration(registry_t *registry, const char *name,
                                           size_t name_size)
{
    for (size_t index = 0U; index < registry->count; ++index)
    {
        feature_entry_t *configuration = registry->configurations[index];
        if (strlen(configuration->name) == name_size &&
            memcmp(configuration->name, name, name_size) == 0)
        {
            return configuration;
        }
    }
    return NULL;
}

/**
 * @brief Reads the sensor configuration registry from NVS.
 *
 * @param registry Pointer to the registry to populate.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
esp_err_t registry_read(registry_t *registry)
{
    if (registry == NULL || registry->configurations == NULL || registry->count == 0U ||
        registry->matches == NULL || registry->match_capacity < registry->count)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t serialized_size = 0U;
    err = nvs_get_blob(nvs_handle, SENSOR_CONFIG_LOOKUP_NVS_KEY, NULL, &serialized_size);
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        return err;
    }
    if (serialized_size < sizeof(sensor_config_nvs_header_t))
    {
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *serialized = malloc(serialized_size);
    if (serialized == NULL)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_blob(nvs_handle, SENSOR_CONFIG_LOOKUP_NVS_KEY, serialized,
                       &serialized_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK)
    {
        free(serialized);
        return err;
    }

    sensor_config_nvs_header_t header;
    memcpy(&header, serialized, sizeof(header));
    if (header.magic != SENSOR_CONFIG_LOOKUP_NVS_MAGIC ||
        header.version != SENSOR_CONFIG_LOOKUP_NVS_VERSION || header.count != registry->count)
    {
        free(serialized);
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t *cursor = serialized + sizeof(header);
    const uint8_t *const end = serialized + serialized_size;
    for (size_t index = 0U; index < header.count; ++index)
    {
        if ((size_t)(end - cursor) < sizeof(sensor_config_nvs_record_t))
        {
            err = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
        sensor_config_nvs_record_t record;
        memcpy(&record, cursor, sizeof(record));
        cursor += sizeof(record);
        size_t record_size = 0U;
        if (!size_add(&record_size, record.name_size) ||
            !size_add(&record_size, record.type_size) ||
            !size_add(&record_size, record.settings_size) ||
            record.name_size == 0U || record.type_size == 0U ||
            record.settings_size == 0U ||
            record_size > (size_t)(end - cursor))
        {
            err = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        feature_entry_t *configuration =
            find_configuration(registry, (const char *)cursor, record.name_size);
        const uint8_t *type = cursor + record.name_size;
        bool duplicate_name = false;
        for (size_t previous = 0U; previous < index; ++previous)
        {
            if (registry->matches[previous] == configuration)
            {
                duplicate_name = true;
                break;
            }
        }
        if (configuration == NULL || duplicate_name ||
            strlen(configuration->type) != record.type_size ||
            memcmp(configuration->type, type, record.type_size) != 0 ||
            configuration->configuration == NULL ||
            configuration->configuration_size != record.settings_size)
        {
            err = ESP_ERR_INVALID_STATE;
            goto cleanup;
        }
        registry->matches[index] = configuration;
        cursor += record_size;
    }
    if (cursor != end)
    {
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    cursor = serialized + sizeof(header);
    for (size_t index = 0U; index < header.count; ++index)
    {
        sensor_config_nvs_record_t record;
        memcpy(&record, cursor, sizeof(record));
        cursor += sizeof(record);
        feature_entry_t *configuration =
            find_configuration(registry, (const char *)cursor, record.name_size);
        cursor += record.name_size + record.type_size;
        memcpy(configuration->configuration, cursor, record.settings_size);
        cursor += record.settings_size;
    }

    err = ESP_OK;

cleanup:
    free(serialized);
    return err;
}

registry_t *registry_init(void)
{
    registry.configurations = configuration_storage;
    registry.capacity = REGISTRY_INITIAL_CAPACITY;
    registry.count = 0U;
    registry.matches = match_storage;
    registry.match_capacity = REGISTRY_INITIAL_CAPACITY;

    if (registry_read(&registry) == ESP_OK)
    {
        return &registry;
    }

    /* A missing or invalid persisted registry must not leave stale pointers
     * available to callers.  Start with an empty registry so MQTT can build
     * a new configuration. */
    memset(configuration_storage, 0, sizeof(configuration_storage));
    memset(match_storage, 0, sizeof(match_storage));
    registry.count = 0U;
    return &registry;
}
