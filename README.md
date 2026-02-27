# shotStopper

An ESP32-based Bluetooth gateway that connects to a compatible BLE scale and automatically stops espresso shots when the target weight is reached.

## Origin

This project is a fork of the `shotStopper` example included in the [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) library by Tate Mazer.

This repository uses the forked library maintained by the [CleverCoffee](https://github.com/rancilio-pid/AcaiaArduinoBLE) project, which has been ported from ArduinoBLE to NimBLE to reduce the memory footprint and add support for additional scales.

## Overview

The system monitors weight in real-time, applies a linear prediction algorithm to account for post-brew drip, and triggers the machine's brew switch at the calculated moment. When no scale is connected, it falls back to time-based brewing using a configurable target time.

A BLE server exposes device settings as characteristics, compatible with the [shotStopper Companion App](https://github.com/icapurro/shotStopperCompanionApp).

## Features

- Automatic shot termination based on weight prediction
- Time-based fallback mode when no scale is connected
- Companion app support via BLE for reading and writing device settings
- Reed switch and momentary switch support
- Auto-tare on shot start
- Persistent configuration stored on LittleFS
- Structured logging with configurable log levels
- Web-based configuration interface (work in progress)
- WiFi configuration via WiFiManager captive portal

## Hardware

### Supported Boards

| Board | PlatformIO target |
|-------|-------------------|
| ESP32-S3 | `esp32-s3-devkitc-1` |
| ESP32-C3 | `esp32-c3-devkitc-02` |

### Pin Assignments

| Function | ESP32-S3 | ESP32-C3 |
|----------|----------|----------|
| LED Red | GPIO 46 | GPIO 21 |
| LED Green | GPIO 47 | GPIO 20 |
| LED Blue | GPIO 45 | GPIO 10 |
| Brew trigger (IN) | GPIO 21 | GPIO 8 |
| Brew stop (OUT) | GPIO 38 | GPIO 6 |
| Reed switch (REED_IN) | GPIO 18 | GPIO 7 |

### Supported Scales

The system supports scales compatible with the AcaiaArduinoBLE protocol, including:

- Acaia Pyxis
- Acaia Lunar
- Acaia Pearl
- Bookoo Themis
- Bookoo Themis Ultra
- WeighMyBru DIY Scale
- Espressi Scale

## Building

This is a PlatformIO project. To build and upload:

| Environment    | Description                       |
|----------------|-----------------------------------|
| `esp32-c3`     | ESP32-C3, USB serial upload       |
| `esp32-s3`     | ESP32-S3, USB serial upload       |

```
pio run -e esp32-s3 -t upload
```

To upload the LittleFS filesystem image (required for default configuration):

```
pio run -e esp32-c3 -t uploadfs
```
## Configuration

Configuration is stored as JSON on the LittleFS filesystem and loaded at startup. Settings can be modified via:

- The [shotStopper Companion App](https://github.com/icapurro/shotStopperCompanionApp) over BLE
- Editing `config.json` on the LittleFS filesystem

Parameters include goal weight, weight offset, brew pulse duration, drip delay, target time, min/max shot duration, switch type, reed switch mode, auto-tare, OTA hostname, and log level. See `Config.h` for the full list of parameters and their defaults.

## License

Released under the MIT License.

## Credits

- Original example by Tate Mazer: https://github.com/tatemazer/AcaiaArduinoBLE
- NimBLE port by the CleverCoffee project: https://github.com/rancilio-pid/AcaiaArduinoBLE
