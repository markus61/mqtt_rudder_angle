# ManT1S Angle

ESP32 firmware for the Silicognition ManT1S angle-sensor device. It targets an ESP32-PICO-V3-02 with a Microchip LAN8671 10BASE-T1S Ethernet PHY, publishes sensor data over MQTT, and supports runtime sensor configuration persisted in NVS.

Features OTA updates, runtime sensor configuration, and MQTT-based data publishing. 

## Build

Install and source ESP-IDF v6.1 or later, then configure and build a preset:

```sh
. $HOME/.espressif/v6.1/esp-idf/export.sh
cmake --preset default
cmake --build build/default
```

Use `production` in place of `default` for the production configuration. Flash
and monitor with the usual ESP-IDF tools for the selected build directory.

## Test

Run the host-side registry tests with:

```sh
bash tests/host/run.sh
```

## Providers

The firmware supports multiple sensor providers, which can be configured at runtime. Each provider is responsible for interfacing with a specific type of sensor and publishing its data over MQTT.

See [main/PROVIDERS.md](main/PROVIDERS.md) for the sensor-provider interface
and MQTT configuration behavior.
