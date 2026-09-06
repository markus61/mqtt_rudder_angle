#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply a configuration document received from the broker.
 *
 * The document must carry a "now" member holding an RFC3339 timestamp, which
 * is used to set the device clock.
 *
 * @param payload Null-terminated configuration document.
 * @return true only if the device clock was set from "now", so the caller can
 *         stop listening for it; false if the payload was rejected.
 */
bool configure_this_device(const char *payload);

#ifdef __cplusplus
}
#endif
