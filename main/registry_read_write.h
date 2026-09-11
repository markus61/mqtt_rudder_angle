#pragma once

#include "esp_err.h"
#include "sensor_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** Store all registered sensor configurations in NVS. */
    esp_err_t registry_write(const registry_t *lookup);

    /** Restore a caller-owned, registered sensor configuration registry from NVS. */
    esp_err_t registry_read(registry_t *registry);

    /** Initialize the sensor agnostic configuration registry. */
    registry_t *registry_init(void);

#ifdef __cplusplus
}
#endif
