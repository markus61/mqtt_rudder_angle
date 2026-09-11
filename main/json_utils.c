#include "json_utils.h"

#include <stdio.h>

bool json_obj_set_escaped_string(json_gen_str_t *json, const char *name, const char *value)
{
    if (json == NULL || name == NULL || value == NULL ||
        json_gen_obj_start_long_string(json, name, NULL) != 0)
    {
        return false;
    }

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; ++cursor)
    {
        const char *escaped = NULL;
        char encoded[7];
        switch (*cursor)
        {
        case '\"':
            escaped = "\\\"";
            break;
        case '\\':
            escaped = "\\\\";
            break;
        case '\b':
            escaped = "\\b";
            break;
        case '\f':
            escaped = "\\f";
            break;
        case '\n':
            escaped = "\\n";
            break;
        case '\r':
            escaped = "\\r";
            break;
        case '\t':
            escaped = "\\t";
            break;
        default:
            if (*cursor < 0x20U)
            {
                (void)snprintf(encoded, sizeof(encoded), "\\u%04x", *cursor);
                escaped = encoded;
            }
            else
            {
                encoded[0] = (char)*cursor;
                encoded[1] = '\0';
                escaped = encoded;
            }
            break;
        }

        if (json_gen_add_to_long_string(json, escaped) != 0)
        {
            return false;
        }
    }

    return json_gen_end_long_string(json) == 0;
}
