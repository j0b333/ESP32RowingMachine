# ESP32 Rowing Monitor

A smart rowing machine monitor firmware for ESP32-S3 that transforms a rowing machine into a connected fitness device with Bluetooth FTMS support and a real-time web interface.

## Features

- 📱 **Bluetooth FTMS** - Works with Kinomap, EXR, MyHomeFit, and other fitness apps
- 🌐 **Web Interface** - Real-time metrics via WiFi on any browser
- ❤️ **Heart Rate Support** - Connects to BLE heart rate monitors
- 📊 **Accurate Metrics** - Physics-based power, pace, and distance calculations
- 💾 **Session Storage** - Saves workouts with full data for later sync

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

1. Connect to WiFi: **CrivitRower** (password: `rowing123`)
2. Open browser: **http://192.168.4.1**
3. Start rowing!

## Documentation

📚 **[Full Documentation](docs/README.md)** - Complete guides and references

| Guide | Description |
|-------|-------------|
| [Setup Guide](docs/SETUP.md) | Hardware wiring, building, configuration |
| [API Reference](docs/API.md) | REST endpoints and WebSocket interface |
| [Architecture](docs/ARCHITECTURE.md) | System design and modules |
| [Physics Model](docs/PHYSICS_MODEL.md) | How metrics are calculated |

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
