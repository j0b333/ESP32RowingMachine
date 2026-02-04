# ESP32 Rowing Monitor

A smart rowing machine monitor firmware for ESP32-S3 that transforms a rowing machine into a connected fitness device with Bluetooth FTMS support and a real-time web interface.

## Features

-  **Bluetooth FTMS** - Works with Kinomap, EXR, MyHomeFit, and other fitness apps
-  **Web Interface** - Real-time metrics via WiFi on any browser
-  **Heart Rate Support** - Connects to BLE heart rate monitors
-  **Accurate Metrics** - Physics-based power, pace, and distance calculations
-  **Session Storage** - Saves workouts with full data for later sync

## Quick Start

### Hardware

| Component | Connection |
|-----------|------------|
| ESP32-S3 DevKitC-1 | USB-C for power |
| Flywheel reed switch | GPIO 15 + GND |
| Seat reed switch | GPIO 16 + GND |

### Build & Flash

Requires [ESP-IDF v6.0+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/).

```bash
git clone https://github.com/j0b333/ESP32RowingMachine.git
cd ESP32RowingMachine
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Connect

1. Connect to WiFi: **CrivitRower** (password: **12345678**)
2. Open browser: **http://192.168.4.1**
3. Connect it to your local wireless network or use it directly using http://rower.local
3. Start rowing!

## Documentation

**[Full Documentation](docs/README.md)** - Complete guides and references

| Guide | Description |
|-------|-------------|
| [Setup Guide](docs/SETUP.md) | Hardware wiring, building, configuration |
| [API Reference](docs/API.md) | REST endpoints and WebSocket interface |
| [Architecture](docs/ARCHITECTURE.md) | System design and modules |
| [Physics Model](docs/PHYSICS_MODEL.md) | How metrics are calculated |

## Screenshots

### Row Tab
Real-time rowing metrics display with power, distance, pace, and heart rate.

![Row Tab](https://github.com/user-attachments/assets/f5dab6b6-4639-4986-96d3-d330575e7e29)

### Graph Tab
Live graphs showing pace, power, and heart rate over time.

![Graph Tab](https://github.com/user-attachments/assets/6b46884a-85c8-4afe-8d42-0f5970f2de42)

### History Tab
View and manage your saved workout sessions.

![History Tab](https://github.com/user-attachments/assets/e47ae13f-2b65-4416-a2aa-e57ef38419a4)

### Settings Tab
Configure weight, heart rate zones, units, and advanced options.

![Settings Tab](https://github.com/user-attachments/assets/6a428dea-120d-4bbf-8cef-06b5670e8122)

## Companion App

Sync workouts to Samsung Health / Google Fit:
**[ESP32RowingMachineCompanionApp](https://github.com/j0b333/ESP32RowingMachineCompanionApp)**

## License

MIT License - See [LICENSE](LICENSE) for details.

## Acknowledgments

This project is inspired by and builds upon the work of the open source community. See [Attributions](docs/ATTRIBUTIONS.md) for credits including:

- [OpenRowingMonitor](https://github.com/JaapvanEkwortel/openrowingmonitor) - Physics algorithms
- [ESP-IDF](https://github.com/espressif/esp-idf) - Espressif IoT Development Framework
- [NimBLE](https://github.com/apache/mynewt-nimble) - Bluetooth stack


## AI Disclaimer 

This was done purely as a hobby project for my own usage. Therefore it was also almost completly vibe coded. You are free to use or add on to it as you see fit but I cannot guarantee great code.
