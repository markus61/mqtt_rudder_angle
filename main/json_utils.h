#pragma once

#include <stdbool.h>

#include "json_generator.h"

/**
 * Add a string member while escaping its value according to JSON's string
 * grammar.  json_generator deliberately leaves string escaping to callers.
 */
bool json_obj_set_escaped_string(json_gen_str_t *json, const char *name, const char *value);
