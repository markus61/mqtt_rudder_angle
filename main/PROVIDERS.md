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
| `size_t <prefix>_config_size(void)` | Return the fixed configuration record size. Do not persist pointers, task handles or callbacks. |
| `bool <prefix>_configure(const cJSON *)` | Validate and apply an MQTT configuration. Do not start a task here. Rejected changes are restored by the registry. |
| `void <prefix>_config_restore(const void *)` | Copy a trusted binary record to live configuration. No MQTT validation. Used at boot and for failed configure/start rollback. |
| `esp_err_t <prefix>_start(void)` | Start the configured provider; repeated calls must be safe. Clean up partial startup on failure. |
| `bool <prefix>_config_to_json(json_gen_str_t *)` | Append the provider's settings to an open JSON object. The registry writes name/type. Escape strings and propagate buffer failures. |
| `void <prefix>_control_action(const char *, int)` | Parse and handle MQTT commands for this provider. The payload is not NUL-terminated. |

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
NVS data. A missing NVS record leaves an empty registry. An unreadable or
unsupported snapshot is reported; NVS is not erased, and MQTT can configure anew.
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
must remain serialized. Every compiled-in provider gets its own topic:
`control/<device_name>/<provider_name>`, where `provider_name` is its API
prefix (for example `angle_sensor` or `uptime_sensor`). The MQTT layer routes
only by topic; providers parse their own actions. Configuration snapshots avoid reading live
provider fields during JSON output, but providers remain responsible for safe
access to live settings from their own tasks.

The current providers retain the existing `configure_feature`, `braindump`, and
`reset` actions. A `configure_feature` request must use the matching provider
topic and a type that provider serves.

A `braindump` reply is symmetric with its request topic. `control/<device>`
returns device state on `control_reply/<device>`, while
`control/<device>/<provider>` returns only that provider's attached
configuration on `control_reply/<device>/<provider>`.
Device replies include `runtime.active_sensors`, containing the API names of
providers whose `start()` call succeeded.

The device action `{"action":"providers"}` publishes every compiled-in
provider and its supported model types to `control_reply/<device>`, regardless
of its configuration or startup state.

Example uptime action (published to `control/<device_name>/uptime_sensor`):

```json
{"action":"configure_feature","name":"device_uptime","type":"dummy_uptime","interval":60,"sensor_topic":"sensors/uptime"}
```

Run `bash tests/host/run.sh` for registry lifecycle tests with fake hardware/NVS,
real dispatch, JSON generation and persistence, under AddressSanitizer and
UndefinedBehaviorSanitizer. Build the firmware with the installed ESP-IDF
environment and `cmake --build build/default`.
