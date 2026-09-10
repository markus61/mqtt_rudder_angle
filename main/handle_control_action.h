#pragma once

/** Parse and dispatch an MQTT control-action JSON payload. */
void handle_control_action(const char *payload, int payload_length);
