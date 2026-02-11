# shotStopper

An ESP32-based Bluetooth gateway for compatible BLE scales to enable brew-by-weight for supported machines.

## Overview

shotStopper connects to a compatible BLE scale and automatically stops espresso shots when the target weight is reached. The system monitors weight in real-time, applies predictive algorithms to account for post-brew drip, and triggers the machine's brew switch at the right moment.

## Origin

This project is a fork of the example included in the AcaiaArduinoBLE library by Tate Mazer:
https://github.com/tatemazer/AcaiaArduinoBLE

It uses the forked AcaiaArduinoBLE library from the CleverCoffee project:
https://github.com/rancilio-pid/AcaiaArduinoBLE

The CleverCoffee fork has been ported from ArduinoBLE to NimBLE to reduce memory footprint and add support for additional scale models.

## Features

- **Automatic shot termination** based on weight targets
- **Predictive weight compensation** to account for post-brew drip
- **Time-based fallback mode** when no scale is connected
- **Web-based configuration interface** for all parameters (Work in progress)
- **Multi-scale support** via NimBLE stack (Acaia Pyxis, Lunar, Pearl, Bookoo Themis, Themis Ultra, etc.)
- **Reed switch support**
- **WiFi configuration portal** for network setup (Work in progress)
- **Persistent configuration** stored in LittleFS filesystem
- **Structured logging system** with configurable log levels

## Hardware Support

### Supported Boards

- ESP32-S3 (esp32-s3-devkitc-1)
- ESP32-C3 (esp32-c3-devkitc-02)

### Pin Assignments

#### ESP32-S3
- LED_RED: GPIO 46
- LED_GREEN: GPIO 47
- LED_BLUE: GPIO 45
- IN (Brew trigger): GPIO 21
- OUT (Brew stop): GPIO 38
- REED_IN (Reed switch): GPIO 18

#### ESP32-C3
- LED_RED: GPIO 21
- LED_GREEN: GPIO 20
- LED_BLUE: GPIO 10
- IN (Brew trigger): GPIO 8
- OUT (Brew stop): GPIO 6
- REED_IN (Reed switch): GPIO 7

### Supported Scales

The system supports Acaia-compatible scales including:
- Acaia Pyxis
- Acaia Lunar
- Acaia Pearl
- Bookoo Themis
- Bookoo Themis Ultra
- WeighMyBru DIY Scale
- Espressi Scale

## License 

Released under the MIT License.

## Credits

- Original example by Tate Mazer
- AcaiaArduinoBLE library: https://github.com/tatemazer/AcaiaArduinoBLE
- NimBLE port by CleverCoffee project: https://github.com/rancilio-pid/AcaiaArduinoBLE
- Tested with Acaia Pyxis and La Marzocco GS3

