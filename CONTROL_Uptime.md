# Uptime-provider control

This document covers the `dummy_uptime` provider, which periodically publishes
the elapsed seconds since the device booted.

## Topics

For a device named `baby_mant1s` and an uptime provider named `uptime`:

- Send provider actions to `control/baby_mant1s/uptime`.
- Receive replies on `control_reply/baby_mant1s/uptime`.
- Read telemetry from the configured `sensor_topic` (by default,
  `sensors/uptime`).

Provider names contain only ASCII letters, digits, `_`, and `-`. A
`configure` action must include `name`, even if the name is not changing.
On a rename, use the new name for subsequent control and reply topics.

## Provider actions

| Action | Effect |
| --- | --- |
| `configure` | Validate, apply, start, and persist a configuration overlay. |
| `braindump` | Publish the active name, type, and configuration. |
| `help` | Publish a short usage message. |
| `deactivate` | Stop the provider and retain its configuration. This is a registry action. |
| `remove` | Stop and permanently remove the provider. This is a registry action. |

`deactivate` and `remove` are handled by the common provider-control layer.
An inactive provider can be restored through the device topic with
`{"action":"activate","name":"uptime"}`.

## Configure

All provider settings are optional, but `name` is required. Omitted settings
retain their existing values; a rejected value leaves the whole configuration
unchanged. `type` may be omitted or repeated with `dummy_uptime`, but cannot
be changed.

```bash
mosquitto_pub -t control/baby_mant1s/uptime \
  -m '{"action":"configure","name":"uptime","interval":30,"sensor_topic":"sensors/uptime"}'
```

| Field | Value |
| --- | --- |
| `name` | Required provider name; may rename this provider to an unused safe name. |
| `type` | Optional `dummy_uptime`; no other type is accepted. |
| `interval` | Positive whole number of seconds; maximum `INT_MAX / 1000` seconds. Default: 60. |
| `sensor_topic` | Non-empty MQTT telemetry topic, at most 63 characters. Default: `sensors/uptime`. |

Each telemetry payload has this shape:

```json
{"now":"2026-09-17T12:00:00Z","uptime":123.45678}
```

`uptime` is elapsed seconds from boot, not wall-clock time.

## Inspect configuration

```bash
mosquitto_pub -t control/baby_mant1s/uptime -m '{"action":"braindump"}'
mosquitto_sub -t control_reply/baby_mant1s/uptime
```

The reply contains `name`, `type`, `interval`, and `sensor_topic`.
