#pragma once

#include <stdbool.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Set the system clock from the RFC3339 "now" member of a JSON object. */
bool device_utils_clock_set(const cJSON *configuration);

/** Download and apply an OTA firmware update from the supplied URL. */
void device_utils_apply_ota_update_request(const char *firmware_url);

/** Log a single member of a configuration JSON object. */
void device_utils_log_configuration_item(const cJSON *item);

#ifdef __cplusplus
}
#endif
