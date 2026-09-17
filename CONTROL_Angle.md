# Angle-provider control

This document covers the `elobau_424A11A040B` and
`elobau_424A11A060B` angle-sensor provider.

## Topics

For a device named `baby_mant1s` and an angle provider named `rudder`:

- Send provider actions to `control/baby_mant1s/rudder`.
- Receive replies on `control_reply/baby_mant1s/rudder`.
- Read measurements from the configured `sensor_topic` (by default,
  `sensors/rudders/starboard`).

Provider names contain only ASCII letters, digits, `_`, and `-`. A
`configure` action must include `name`, even if the name is not changing.
On a rename, use the new name for subsequent control and reply topics.

## Provider actions

| Action | Effect |
| --- | --- |
| `configure` | Validate, apply, start, and persist a complete configuration overlay. |
| `calibrate` | Expand the configured voltage range to the observed extremes; publishes a reply only for each changed bound. |
| `calibration_check` | Publish observed voltage extremes and whether they exceed the configured range. |
| `braindump` | Publish the active name, type, and configuration. |
| `help` | Publish a short usage message. |
| `deactivate` | Stop the provider and retain its configuration. This is a registry action. |
| `remove` | Stop and permanently remove the provider. This is a registry action. |

`deactivate` and `remove` are handled by the common provider-control layer.
An inactive provider can be restored through the device topic with
`{"action":"activate","name":"rudder"}`.

## Configure

All `sensor_*` fields are optional. Fields omitted from the request keep their
existing values. The update is atomic: an invalid supplied field rejects the
whole request. `type` may be omitted or repeated with its current value, but
cannot be changed.

```bash
mosquitto_pub -t control/baby_mant1s/rudder \
  -m '{"action":"configure","name":"rudder","sensor_deadband_millivolt":20}'
```

| Field | Value |
| --- | --- |
| `name` | Required provider name; may rename this provider to an unused safe name. |
| `type` | Optional exact current type; cannot change it. |
| `sensor_pin` | An ADC1-capable GPIO. The default is GPIO32. |
| `sensor_sample_period_ms` | Positive integer sample period in milliseconds. |
| `sensor_samples_per_reading` | Positive integer raw samples per published reading. |
| `sensor_minimum_millivolts` | Integer lower endpoint of the ADC/front-end voltage range. |
| `sensor_maximum_millivolts` | Integer upper endpoint of the ADC/front-end voltage range. |
| `sensor_deadband_millivolt` | Integer change threshold before publishing another angle. |
| `sensor_minimum_degrees` | Numeric lower angle endpoint. |
| `sensor_maximum_degrees` | Numeric upper angle endpoint. |
| `sensor_center_degrees` | Numeric centre angle. |
| `sensor_topic` | Non-empty MQTT telemetry topic, at most 63 characters. |

The sensor is a 4--20 mA model. The millivolt values are the voltages at the
ADC after the board's current-to-voltage front end, not the sensor's loop
current. For example, a 165 ohm burden produces 660--3300 mV for 4--20 mA.

## Calibration and inspection

Move the sensor through the required travel first, then inspect and apply the
observed limits:

```bash
mosquitto_pub -t control/baby_mant1s/rudder -m '{"action":"calibration_check"}'
mosquitto_pub -t control/baby_mant1s/rudder -m '{"action":"calibrate"}'
```

`calibration_check` replies with `calibration_required`,
`calibration_min_value`, and `calibration_max_value`. `calibrate` only widens
the configured range; it does not shrink an existing range.

## Inspect configuration

```bash
mosquitto_pub -t control/baby_mant1s/rudder -m '{"action":"braindump"}'
mosquitto_sub -t control_reply/baby_mant1s/rudder
```

The reply includes `name`, `type`, and every current `sensor_*` setting.
