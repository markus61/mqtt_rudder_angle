# Sensor providers

## Provider implementation API

Implement the API exposed by `adc/elobau_angle_sensor.h` and
`dummy/uptime_sensor.h`, include the new header in `sensor_config.c`, and add
`SENSOR_PROVIDER(your_prefix)` to `providers[]`. Add its sources and include
directory to `main/CMakeLists.txt`.

The compiled-in catalogue contains factories, not live sensors. A factory may
create any number of independent instances of its supported model types.

| Operation | Responsibility |
| --- | --- |
| `can_serve_type(type)` | Recognize exact model strings without side effects. Multiple factories claiming one type is an error. |
| `supported_type_count()` / `supported_type(index)` | Enumerate model strings exposed by the device-level `providers` action. Index zero supplies that factory's bootstrap default type. |
| `create(type)` / `destroy(instance)` | Allocate defaults for the requested type and release all instance-owned resources. Return `NULL` for an unsupported type or allocation failure. |
| `config_get(instance)` / `config_size()` | Borrow the instance's plain-data persistent configuration and report its fixed size. Never persist pointers, task handles, callbacks, or driver handles. |
| `configure(instance, json)` | Atomically validate and apply an MQTT overlay. Omitted provider settings retain their values. The instance type cannot change. Do not start a task here. |
| `config_restore(instance, record)` | Restore one trusted snapshot at boot or during rollback. Verify that its embedded model type still matches the instance. |
| `start(instance)` | Start that instance. Repeated calls must be safe; clean up partial startup on failure. |
| `config_to_json(instance, json)` | Append provider settings to an open object. The registry writes `name` and `type`. |
| `working_topics_json_add(instance, json)` | Append normal publication topics to an open array. Do not include common control-reply topics. |
| `control_action(instance, name, payload, length)` | Handle the non-NUL-terminated payload for the named instance. Use `name` for replies and provider-scoped `configure`. |

All mutable provider state must belong to the instance: configuration, task
handles, readings, calibration state, and per-channel driver handles. Truly
device-wide resources may be shared explicitly; the angle provider shares its
ADC1 unit while keeping channels and calibration state per instance.

The registry owns copies of every instance's name, model type, and binary
configuration snapshot. Incoming MQTT JSON can therefore be deleted as soon as
an action returns. Registry capacity is currently 10 instances.

## Persistence and boot

The persistent registry is the complete source of truth for named sensors. Each
record contains `name`, `type`, and provider-specific settings. Numeric names
have no special representation and obey the same uniqueness rules as all other
names.

When no registry exists, boot creates one instance from each factory's default
configuration, assigns consecutive names (`"1"`, `"2"`, ...), starts them, and
persists the complete initial registry. When a registry exists, boot creates
exactly its recorded instances; it does not synthesize missing provider types.

The NVS version-1 layout already supports multiple records of the same type and
is unchanged. Boot validates every name, type, and binary record size before
restoring provider settings. Stored order remains stable. An unreadable,
unsupported, or incompatible registry fails initialization; the
application-level incompatibility policy then applies.

MQTT control and mutation run on the MQTT event task and remain serialized. A
successful runtime start remains visible if the following NVS commit fails, so
the same instance can retry persistence with `configure`. Provider removal and
hardware-conflict policy are intentionally outside the current implementation.

## MQTT actions

Device actions are published to `control/<device>`.

`providers` returns the compiled-in factory catalogue and supported types:

```json
{"action":"providers"}
```

`add_provider` requires an available model `type` and a globally unique `name`.
Provider-specific initial settings may be supplied in the same atomic action:

```json
{
  "action": "add_provider",
  "type": "elobau_424A11A040B",
  "name": "port_rudder",
  "sensor_pin": 33,
  "sensor_topic": "sensors/rudders/port",
  "sensor_sample_period_ms": 100
}
```

The name must contain 1-63 ASCII letters, digits, `_`, or `-`. Separators,
wildcards, whitespace, non-ASCII characters, and every already occupied name
(including numeric names) are rejected. The instance is created from defaults,
the supplied settings are validated and applied, it is started, its control
topic is subscribed, and its complete record is persisted.

Provider actions use `control/<device>/<name>`. `configure` remains
provider-scoped: it updates the addressed instance, may rename it to another
unique safe name, and persists its snapshot. If renamed, MQTT subscribes the new
topic before unsubscribing the old one. Its type may be omitted or repeated but
cannot change.

```json
{
  "action": "configure",
  "name": "device_uptime",
  "interval": 60,
  "sensor_topic": "sensors/uptime"
}
```

Replies use `control_reply/<device>/<name>`. A provider `braindump` includes
that instance's name, type, and settings. Device braindumps expose every started
instance in `runtime.active_sensors`, including its working topics.

`reset` remains exclusively device-scoped. Provider removal is not yet
implemented.

## Verification

Run `bash tests/host/run.sh` for registry lifecycle tests under AddressSanitizer
and UndefinedBehaviorSanitizer. Enable LeakSanitizer where supported with
`MANT1S_ENABLE_LEAK_CHECKS=1 bash tests/host/run.sh`.

Build the firmware with `cmake --build build/default`.
