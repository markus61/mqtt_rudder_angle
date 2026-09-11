#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Identity and configuration for one configured sensor.
     *
     * @a configuration points to the sensor-specific configuration (for example,
     * an angle_sensor_config_t).  The registry deliberately does not inspect
     * it, allowing one list to contain different kinds of sensors.
     *
     * The name and type strings must remain valid while the configuration is
     * registered.
     */
    typedef struct feature_entry
    {
        const char *name;
        const char *type;
        void *configuration;
        size_t settings_size;
    } feature_entry_t;

    /** Backward-compatible name for a registry feature entry. */
    typedef feature_entry_t sensor_config_t;

    /** A non-owning array of configurations returned by a lookup. */
    typedef struct
    {
        feature_entry_t *const *items;
        size_t count;
    } registry_lookup_result_t;

    /**
     * @brief Fixed-capacity, allocation-free sensor configuration registry.
     *
     * Callers provide both pointer arrays, so the registry can be used before
     * a heap is available.  @a match_storage must have room for every entry
     * that may be registered.  A result remains valid until the next lookup
     * using the same registry or until that registry is changed.
     */
    typedef struct
    {
        feature_entry_t **configurations;
        size_t capacity;
        size_t count;
        feature_entry_t **matches;
        size_t match_capacity;
    } registry_t;

    /** Initialise @a lookup with caller-owned pointer storage. */
    bool sensor_config_lookup_init(registry_t *lookup,
                                   feature_entry_t *configuration_storage[],
                                   size_t configuration_capacity,
                                   feature_entry_t *match_storage[],
                                   size_t match_capacity);

    /**
     * Register @a configuration.  Names are unique; equal types are allowed.
     *
     * @return false for invalid input, a duplicate name, or a full registry.
     */
    bool sensor_config_lookup_add(registry_t *lookup,
                                  feature_entry_t *configuration);

    /** Return the sole configuration with @a name, or an empty result. */
    registry_lookup_result_t
    sensor_config_lookup_by_name(registry_t *lookup, const char *name);

    /** Return every configuration with @a type, or an empty result. */
    registry_lookup_result_t
    sensor_config_lookup_by_type(registry_t *lookup, const char *type);

    /** Write every registered sensor configuration, including its name, as a JSON array. */
    size_t sensor_json_dump(char *buffer, size_t buffer_size);

    esp_err_t registry_init_on_boot(void);

    /** Borrow the registry created during boot initialization. */
    registry_t *registry_active_lookup(void);

    esp_err_t sensor_config_from_mqtt(const cJSON *action_json);

#ifdef __cplusplus
}
#endif
