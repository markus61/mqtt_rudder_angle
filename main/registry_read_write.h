#pragma once

#include "esp_err.h"
#include "sensor_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** Store all registered sensor configurations in NVS. */
    esp_err_t registry_write(const registry_t *lookup);

    /** Replace entries from NVS atomically; leave the registry intact on failure. */
    esp_err_t registry_read(registry_t *registry);

    /** Return the process registry, initially empty. Does not read NVS. */
    registry_t *registry_init(void);

    /** Allocate an owned entry by copying its identity and opaque configuration. */
    feature_entry_t *registry_entry_create(const char *name, const char *type,
                                          const void *configuration, size_t size);
    void registry_entry_free(feature_entry_t *entry);
    void registry_clear(registry_t *registry);

#ifdef __cplusplus
}
#endif
