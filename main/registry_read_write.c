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
#define SENSOR_CONFIG_LOOKUP_NVS_VERSION UINT16_C(2)
#define REGISTRY_INITIAL_CAPACITY SENSOR_PROVIDER_MAX_INSTANCES

/* Capacity limits attachments, not model identifiers or provider dispatch. */
static feature_entry_t *configuration_storage[REGISTRY_INITIAL_CAPACITY];
static registry_t registry = {
    .configurations = configuration_storage,
    .capacity = REGISTRY_INITIAL_CAPACITY,
};

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
} sensor_config_nvs_record_v1_t;

typedef struct
{
    uint32_t name_size;
    uint32_t type_size;
    uint32_t settings_size;
    uint32_t state;
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
    if (lookup == NULL || lookup->configurations == NULL ||
        lookup->count > UINT16_MAX || lookup->count > lookup->capacity)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t serialized_size = sizeof(sensor_config_nvs_header_t);
    for (size_t index = 0U; index < lookup->count; ++index)
    {
        const feature_entry_t *configuration = lookup->configurations[index];
        if (configuration == NULL || !valid_text(configuration->name) ||
            !valid_text(configuration->type) || configuration->configuration == NULL ||
            (configuration->state != SENSOR_PROVIDER_ACTIVE &&
             configuration->state != SENSOR_PROVIDER_INACTIVE) ||
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
            .state = (uint32_t)configuration->state,
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


feature_entry_t *registry_entry_create(const char *name, const char *type,
                                      const void *configuration, size_t size)
{
    if (!valid_text(name) || !valid_text(type) || configuration == NULL || size == 0)
    {
        return NULL;
    }
    feature_entry_t *entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        return NULL;
    }
    entry->name = strdup(name);
    entry->type = strdup(type);
    entry->configuration = malloc(size);
    if (entry->name == NULL || entry->type == NULL || entry->configuration == NULL)
    {
        registry_entry_free(entry);
        return NULL;
    }
    entry->configuration_size = size;
    memcpy(entry->configuration, configuration, size);
    return entry;
}

void registry_entry_free(feature_entry_t *entry)
{
    if (entry != NULL)
    {
        free(entry->name);
        free(entry->type);
        free(entry->configuration);
        free(entry);
    }
}

void registry_clear(registry_t *lookup)
{
    for (size_t i = 0; i < lookup->count; ++i)
    {
        registry_entry_free(lookup->configurations[i]);
        lookup->configurations[i] = NULL;
    }
    lookup->count = 0;
}

esp_err_t registry_read(registry_t *lookup)
{
    if (lookup == NULL || lookup->configurations == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIGURATION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }
    size_t size = 0;
    err = nvs_get_blob(handle, SENSOR_CONFIG_LOOKUP_NVS_KEY, NULL, &size);
    if (err != ESP_OK || size < sizeof(sensor_config_nvs_header_t))
    {
        nvs_close(handle);
        return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }
    uint8_t *bytes = malloc(size);
    if (bytes == NULL)
    {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_blob(handle, SENSOR_CONFIG_LOOKUP_NVS_KEY, bytes, &size);
    nvs_close(handle);
    if (err != ESP_OK || size < sizeof(sensor_config_nvs_header_t))
    {
        free(bytes);
        return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }

    sensor_config_nvs_header_t header;
    memcpy(&header, bytes, sizeof(header));
    if (header.magic != SENSOR_CONFIG_LOOKUP_NVS_MAGIC ||
        (header.version != UINT16_C(1) &&
         header.version != SENSOR_CONFIG_LOOKUP_NVS_VERSION) ||
        header.count > lookup->capacity)
    {
        free(bytes);
        return ESP_ERR_INVALID_STATE;
    }

    /* Stage owned records before replacing the live index. Checking framing,
     * sizes and identity here is storage integrity, not provider validation. */
    registry_t staged = {
        .capacity = header.count,
        .configurations = calloc(header.count ? header.count : 1, sizeof(feature_entry_t *)),
    };
    if (staged.configurations == NULL)
    {
        free(bytes);
        return ESP_ERR_NO_MEM;
    }
    const uint8_t *cursor = bytes + sizeof(header);
    const uint8_t *end = bytes + size;
    for (size_t i = 0; i < header.count; ++i)
    {
        const size_t record_header_size =
            header.version == UINT16_C(1) ? sizeof(sensor_config_nvs_record_v1_t)
                                         : sizeof(sensor_config_nvs_record_t);
        if ((size_t)(end - cursor) < record_header_size)
        {
            err = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
        sensor_config_nvs_record_t record = {
            .state = SENSOR_PROVIDER_ACTIVE,
        };
        if (header.version == UINT16_C(1))
        {
            sensor_config_nvs_record_v1_t old_record;
            memcpy(&old_record, cursor, sizeof(old_record));
            record.name_size = old_record.name_size;
            record.type_size = old_record.type_size;
            record.settings_size = old_record.settings_size;
        }
        else
        {
            memcpy(&record, cursor, sizeof(record));
        }
        cursor += record_header_size;
        size_t record_size = 0;
        if (record.name_size == 0 || record.type_size == 0 || record.settings_size == 0 ||
            (record.state != SENSOR_PROVIDER_ACTIVE &&
             record.state != SENSOR_PROVIDER_INACTIVE) ||
            !size_add(&record_size, record.name_size) ||
            !size_add(&record_size, record.type_size) ||
            !size_add(&record_size, record.settings_size) ||
            record_size > (size_t)(end - cursor))
        {
            err = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
        if (memchr(cursor, '\0', record.name_size + (size_t)record.type_size) != NULL)
        {
            err = ESP_ERR_INVALID_ARG;
            goto cleanup;
        }
        char *name = strndup((const char *)cursor, record.name_size);
        char *type = strndup((const char *)cursor + record.name_size, record.type_size);
        if (name == NULL || type == NULL)
        {
            free(name);
            free(type);
            err = ESP_ERR_NO_MEM;
            goto cleanup;
        }
        feature_entry_t *entry = registry_entry_create(
            name, type, cursor + record.name_size + record.type_size, record.settings_size);
        free(name);
        free(type);
        if (entry == NULL)
        {
            err = ESP_ERR_NO_MEM;
            goto cleanup;
        }
        entry->state = (sensor_provider_state_t)record.state;
        for (size_t j = 0; j < staged.count; ++j)
        {
            if (strcmp(staged.configurations[j]->name, entry->name) == 0)
            {
                registry_entry_free(entry);
                err = ESP_ERR_INVALID_STATE;
                goto cleanup;
            }
        }
        staged.configurations[staged.count++] = entry;
        cursor += record_size;
    }
    if (cursor != end)
    {
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    registry_clear(lookup);
    memcpy(lookup->configurations, staged.configurations,
           staged.count * sizeof(feature_entry_t *));
    lookup->count = staged.count;
    staged.count = 0; /* ownership transferred */
    err = ESP_OK;

cleanup:
    registry_clear(&staged);
    free(staged.configurations);
    free(bytes);
    return err;
}

registry_t *registry_init(void)
{
    return &registry;
}
