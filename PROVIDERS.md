# Sensor providers and registry

## Provider implementation API

Implement the API exposed by `main/adc/elobau_angle_sensor.h` and
`main/dummy/uptime_sensor.h`, include the header in `main/sensor_config.c`, and
add `SENSOR_PROVIDER(your_prefix)` to `providers[]`. Add its sources and
include directory to `main/CMakeLists.txt`.

The compiled-in catalogue contains factories, not live sensors. A factory may
create independent instances for any of its supported model types.

| Operation | Responsibility |
| --- | --- |
| `can_serve_type(type)` | Recognize exact model strings without side effects. Multiple factories claiming one type is an error. |
| `supported_type_count()` / `supported_type(index)` | Enumerate model strings exposed by the device-level `providers` action. Index zero is that factory's bootstrap default type. |
| `create(type)` / `destroy(instance)` | Allocate defaults for the requested type and release all instance-owned resources. Return `NULL` for an unsupported type or allocation failure. |
| `config_get(instance)` / `config_size()` | Borrow the instance's plain-data persistent configuration and report its fixed size. Never persist pointers, task handles, callbacks, or driver handles. |
| `configure(instance, json)` | Atomically validate and apply an MQTT overlay. Omitted provider settings retain their values. The instance type cannot change. Do not start a task here. |
| `config_restore(instance, record)` | Restore one trusted snapshot at boot or during rollback. Verify that its embedded model type still matches the instance. |
| `start(instance)` | Start that instance. Repeated calls must be safe and partial startup must be cleaned up. |
| `config_to_json(instance, json)` | Append provider settings to an open object. The registry writes `name` and `type`. |
| `working_topics_json_add(instance, json)` | Append normal publication topics to an open array. Do not include common control-reply topics. |
| `control_action(instance, name, payload, length)` | Handle the non-NUL-terminated payload for the named instance. Use `name` for replies and provider-scoped `configure`. |

All mutable state belongs to the instance: configuration, task handles,
readings, calibration state, and per-channel driver handles. A provider may
explicitly share a truly device-wide resource; angle instances, for example,
share ADC1 while retaining independent channels and calibration state.

## Registry model

The registry is the complete persistent source of truth for sensor instances.
Each record owns and stores:

| Field | Meaning |
| --- | --- |
| `name` | Globally unique MQTT control/reply component. |
| `type` | Exact supported model string used to select a provider factory. |
| `state` | `active` starts and runs the instance; `inactive` retains its identity and settings without a live instance. |
| `settings` | Opaque provider-owned configuration snapshot. |

The registry's in-memory runtime table pairs every record with its selected
factory, live instance, and started state; those runtime values are never
persisted. Registry capacity is currently 10 instances.

Names are 1-63 ASCII letters, digits, `_`, or `-`. Numeric names are ordinary
names: they have no special representation and occupy the same global namespace
as human-readable names.

NVS version 2 stores the state with each record. Version-1 registries are read
as all active and are upgraded by the next registry write. At boot, the registry
validates every persisted name, type, state, and configuration size, then
restores and starts only active instances. Stored order is retained. An
unreadable, unsupported, or incompatible registry fails initialization; the
application-level incompatibility policy then applies.

### Bootstrap

When no registry record exists in NVS, boot creates one instance from each
factory's default configuration, gives them consecutive names (`"1"`, `"2"`,
...), starts them, and persists the complete initial registry. Thereafter boot
creates exactly the records stored in NVS; it does not synthesize provider types
that are absent from an existing registry.

This means a bootstrap instance configured through `control/<device>/1` simply
becomes the persisted record under its configured name. It is not a special
default object after boot.

## MQTT actions

Device actions are published to `control/<device>`.

`providers` returns the compiled-in factory catalogue and supported types:

```json
{"action":"providers"}
```

`add_provider` requires an available model `type` and a globally unique `name`.
It may include provider-specific initial settings; creation, settings
validation, startup, registry insertion, and NVS persistence are one operation.

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

Unknown or ambiguous types, malformed values, duplicate names—including a
currently occupied numeric name—and capacity exhaustion are rejected. On a
successful runtime start, the new named control topic is subscribed. If its NVS
commit fails, the running instance remains visible and a later `configure` can
retry persistence.

Provider actions use `control/<device>/<name>`. `configure` updates the
addressed instance and persists its snapshot; the action does not need a
`name` field. Supplying `name` is only needed to rename it to another unique,
safe name. Its type may be omitted or repeated but cannot change. When renamed,
MQTT subscribes the new topic before removing the old subscription.

`deactivate` is provider-scoped and requires no provider-specific support. It
destroys the live instance, releasing its resources and stopping publications,
marks the registry record inactive, persists it, and removes that instance's
control-topic subscription. Its name, type, and settings remain in the
registry. An inactive instance has no provider control topic.

```json
{"action":"deactivate"}
```

`remove` is provider-scoped and also requires no provider-specific support. It
deactivates the addressed instance, permanently removes its registry entry
(including saved settings), persists the reduced registry, and unsubscribes its
control topic. A removed provider can only be recreated with `add_provider`.

```json
{"action":"remove"}
```

`activate` is device-scoped and requires the `name` of an inactive registry
entry. It recreates that saved instance, restores its settings, starts it,
marks and persists it as active, and subscribes its provider control topic.
Activating an unknown or already active name is rejected.

```json
{"action":"activate","name":"port_rudder"}
```

```json
{
  "action": "configure",
  "interval": 60,
  "sensor_topic": "sensors/uptime"
}
```

Replies use `control_reply/<device>/<name>`. Provider `braindump` replies
include that instance's name, type, and settings. Device braindumps expose all
started instances in `runtime.active_sensors`, including their working topics,
and retained inactive registry entries in `runtime.inactive_sensors` with their
name and type.

`reset` remains device-scoped. Hardware-conflict policy is intentionally
outside the current implementation.

## Verification

Run `bash tests/host/run.sh` for the registry lifecycle tests under
AddressSanitizer and UndefinedBehaviorSanitizer. Enable LeakSanitizer where
supported with `MANT1S_ENABLE_LEAK_CHECKS=1 bash tests/host/run.sh`.

Build the firmware with `cmake --build build/default`.
