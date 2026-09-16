# Changelog

All notable changes to ManT1S Angle are documented in this file.

## 0.1.10 - 2026-09-16

### Changed

- Reworked MQTT control routing so device actions use `control/<device>` and
  sensor actions use `control/<device>/<component>`.
- Moved `reset` exclusively to the device control topic; providers neither
  advertise nor handle a reset action.
- Replaced `configure_feature` with the provider-scoped `configure` action.
- Assigned successfully started sensors consecutive boot-session numbers. A
  sensor initially uses its number as the MQTT control and reply component.
- Made `configure` attach a safe sensor name and move its live control and
  reply topics from the numeric component to that name. Reconfiguring a sensor
  can rename it; the client subscribes to the new topic before unsubscribing
  from the old topic.
- Start every compiled-in provider from its default configuration when it has
  no NVS record. Persisted records start first, in stored order.
- Restricted device and sensor names used in MQTT topics to a single safe topic
  level: 1-63 ASCII letters, digits, `_`, and `-`.
- Centralized sensor attachment, naming, persistence, and MQTT-topic-component
  selection in the generic sensor registry. Provider API names remain internal.
- Made device braindumps expose each started sensor's current `name` and its
  provider-reported `working_topics`, so named control routes are discoverable.

### Fixed

- Propagated provider-control errors through MQTT dispatch instead of silently
  reporting success.
- Migrated provider configure, restore, start, JSON, and control operations to
  return `esp_err_t`; invalid supplied configuration no longer partially applies.
- Kept live control topics synchronized when configuration succeeds at runtime
  but NVS persistence fails.
- Made host sanitizer tests usable in restricted environments by disabling
  LeakSanitizer by default; leak checks remain opt-in with
  `MANT1S_ENABLE_LEAK_CHECKS=1`.

### Documentation and tests

- Updated provider help responses and the MQTT control protocol documentation.
- Extended host registry tests for default numeric routing, safe sensor names,
  configured names, and provider renaming.
