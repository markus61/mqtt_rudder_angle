# Adding a sensor provider

Implement the public API used by `adc/elobau_angle_sensor.h` and
`dummy/uptime_sensor.h`, then include the new public header in `sensor_config.c`
and add `SENSOR_PROVIDER(your_prefix)` to `providers[]`. Add the implementation
files and include directory to `main/CMakeLists.txt`.

No changes to MQTT dispatch, NVS, JSON traversal or `main.c` are needed.
The catalogue lists available implementations; it does not attach or start them.
Attachments and their names/model types come only from MQTT or stored NVS records.

| Operation | Responsibility |
| --- | --- |
| `bool <prefix>_can_serve_type(const char *)` | Recognize exact model strings; return false for NULL/unknown types. Must have no side effects. Multiple providers claiming a model is an error. |
| `size_t <prefix>_supported_type_count(void)` / `const char *<prefix>_supported_type(size_t)` | Enumerate the exact model strings exposed by the device-level `providers` action. Return NULL for an out-of-range index. |
| `const void *<prefix>_config_get(void)` | Borrow the live provider-owned plain-data configuration; lifetime is the entire process. Keep sensible defaults in the provider. |
| `const char *<prefix>_current_type(void)` | Return the fixed hardware type in live configuration. The registry stores it when the provider is first attached. |
| `size_t <prefix>_config_size(void)` | Return the fixed configuration record size. Do not persist pointers, task handles or callbacks. |
| `esp_err_t <prefix>_configure(const cJSON *)` | Validate and apply an MQTT configuration overlay atomically. Provider settings may be omitted to retain their current values. The hardware type is fixed and must not change. Do not start a task here. Return an error without changing live settings when validation fails. |
| `esp_err_t <prefix>_config_restore(const void *)` | Copy a trusted binary record to live configuration. No MQTT validation. Used at boot and for failed configure/start rollback. |
| `esp_err_t <prefix>_start(void)` | Start the configured provider; repeated calls must be safe. Clean up partial startup on failure. |
| `esp_err_t <prefix>_config_to_json(json_gen_str_t *)` | Append the provider's settings to an open JSON object. The registry writes name/type. Escape strings and return a serialization error on failure. |
| `esp_err_t <prefix>_working_topics_json_add(json_gen_str_t *)` | Append the provider's normal post-start publication topics to an open JSON array. Do not include common control-reply topics. Return a serialization error on failure. |
| `esp_err_t <prefix>_control_action(const char *, int)` | Parse and handle MQTT commands for this provider. The payload is not NUL-terminated. Return `ESP_OK` only when the action was accepted; otherwise return the reason for rejection. |

The registry owns copies of names, model types and configuration snapshots, so
incoming MQTT JSON can be deleted immediately. The type is the model string
accepted by `can_serve_type`, not a category such as `angle`.

The current provider API is a singleton: one active configuration and task per
provider. A second name using the same provider is rejected, even when it uses
another supported model. Repeating a name/type updates its existing attachment.
Changing its type is rejected. Independent instances would require an explicit
instance handle throughout the provider API. Registry capacity is currently 10
attachments (`REGISTRY_INITIAL_CAPACITY`).

At boot, NVS reconstructs owned records into an empty registry. After checking
record framing, provider availability, uniqueness and binary sizes, the registry
restores and starts the attached providers. It never invokes `configure()` on
NVS data. A provider absent from the NVS record starts with its provider-owned
default configuration; defaults are runtime state and are not written to NVS.
An unreadable or unsupported snapshot is reported; NVS is not erased, and MQTT
can configure anew.
The existing version-1 record layout is retained. Old records whose type was a
category (`angle`) instead of a supported model require MQTT reconfiguration;
there is no guessed conversion of provider data in the registry.

MQTT validates before starting and persisting. If startup fails, the previous
provider settings are restored and the registry/NVS remain unchanged. If NVS
fails after successful startup, the runtime attachment remains visible and the
error is returned; resending the same action retries persistence. The current
API has no stop operation to undo a successful startup. Provider tasks retain
responsibility for applying changes to running hardware and their timing.

Boot precedes MQTT; provider control operations run on the MQTT event task. They
must remain serialized. Successfully started sensors are assigned consecutive
numbers for that boot: persisted records keep their stored order, then providers
absent from NVS start from defaults in catalogue order. Sensor `1` therefore
initially uses `control/<device_name>/1` and replies on
`control_reply/<device_name>/1`.
Providers that fail to start receive no number or control subscription. Provider
API prefixes such as `angle_sensor` are internal and never appear in MQTT topics.
Configuration snapshots avoid reading live provider fields during JSON output,
but providers remain responsible for safe access to live settings from their own
tasks.

`device_name` is a single MQTT topic level. Configure `your_name` with 1-63
ASCII letters, digits, `_`, or `-`; separators, MQTT wildcards, whitespace, and
non-ASCII characters are rejected.

`reset` is a device action: publish `{"action":"reset"}` to
`control/<device>` to restart the device. Sensor topics never reset the device.
Providers offer `configure` and `braindump` actions. A `configure` request
must use the current sensor topic. Hardware type comes from the provider's
default configuration and cannot be changed. Its
`name` is a safe MQTT topic level: 1-63 ASCII letters, digits, `_`, or `-`.
After configuration, that provider subscribes to
`control/<device_name>/<name>` and stops listening on its old topic; replies
move to `control_reply/<device_name>/<name>`. Repeating `configure` on the
named topic can rename the sensor and moves both topics again. A name already
attached to another provider is rejected.

A `braindump` reply is symmetric with its request topic. `control/<device>`
returns device state on `control_reply/<device>`, while
`control/<device>/<component>` returns only that sensor's configuration on
`control_reply/<device>/<component>`. The component is the boot-session number
until `configure` attaches a name, then that configured name. A default-started
sensor has no configured name, but its reply still carries its type and
configuration. Device replies include `runtime.active_sensors`, with one object
per successfully started sensor. Its `name` is the current control/reply topic
level (the boot number until configured, then the configured name), and
`working_topics` lists normal telemetry topics that the provider may publish
after startup. It deliberately excludes common control-reply topics.

The device action `{"action":"providers"}` publishes every compiled-in
provider and its supported model types to `control_reply/<device>`, regardless
of its configuration or startup state.

Example uptime action (published to `control/<device_name>/2` when the angle
sensor and uptime provider both start successfully):

```json
{"action":"configure","name":"device_uptime","interval":60,"sensor_topic":"sensors/uptime"}
```

Run `bash tests/host/run.sh` for registry lifecycle tests with fake hardware/NVS,
real dispatch, JSON generation and persistence, under AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer is disabled by default because it
requires `ptrace`, which sandboxed runners commonly deny; enable it on a
supported host with `MANT1S_ENABLE_LEAK_CHECKS=1 bash tests/host/run.sh`.
Build the firmware with the installed ESP-IDF environment and
`cmake --build build/default`.
