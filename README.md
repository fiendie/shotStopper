# shotStopper

An ESP32-based Bluetooth gateway that connects to a compatible BLE scale and automatically stops espresso shots when the target weight is reached.

## Origin

This project is a fork of the `shotStopper` example included in the [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) library by Tate Mazer.

This repository uses the forked library maintained by the [CleverCoffee](https://github.com/rancilio-pid/AcaiaArduinoBLE) project, which has been ported from ArduinoBLE to NimBLE to reduce the memory footprint and add support for additional scales.

## Overview

The system monitors weight in real-time, applies a linear prediction algorithm to account for post-brew drip, and triggers the machine's brew switch at the calculated moment. When no scale is connected, it falls back to time-based brewing using a configurable target time.

A BLE characteristic allows updating the goal weight remotely using a generic BLE client such as LightBlue.

## Features

- Automatic shot termination based on weight prediction
- Time-based fallback mode when no scale is connected
- Configurable goal weight via BLE characteristic
- Reed switch and momentary switch support
- Auto-tare on shot start
- Persistent configuration stored on LittleFS
- Structured logging with configurable log levels
- Web-based configuration interface (work in progress)
- WiFi configuration portal (work in progress)

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

```
pio run -e esp32-s3 -t upload
```

Replace `esp32-s3` with `esp32-c3` for the C3 target.

To upload the LittleFS filesystem image (required for default configuration):

```
pio run -e esp32-s3 -t uploadfs
```

## Configuration

Configuration is stored as JSON on the LittleFS filesystem and loaded at startup. Parameters include goal weight, weight offset, brew pulse duration, drip delay, target time, switch type, and logging level. See `Config.h` for the full list of parameters and their defaults.

## License

Released under the MIT License.

## Credits

- Original example by Tate Mazer: https://github.com/tatemazer/AcaiaArduinoBLE
- NimBLE port by the CleverCoffee project: https://github.com/rancilio-pid/AcaiaArduinoBLE
