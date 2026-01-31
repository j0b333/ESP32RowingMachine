# Attributions & Acknowledgments

This project builds upon the work of several open source projects and communities. We gratefully acknowledge their contributions.

## Open Source Projects

### OpenRowingMonitor

**Repository:** [https://github.com/JaapvanEkwortel/openrowingmonitor](https://github.com/JaapvanEkwortel/openrowingmonitor)

The physics algorithms used in this project are inspired by and adapted from OpenRowingMonitor, an open source rowing monitor for Raspberry Pi. OpenRowingMonitor provides a comprehensive implementation of rowing physics including:

- Flywheel physics and angular velocity calculations
- Drag coefficient auto-calibration
- Stroke detection algorithms
- Power and distance calculations using the Concept2-style formula

We thank the OpenRowingMonitor team for their excellent documentation and implementation of rowing physics.

### ESP-IDF

**Repository:** [https://github.com/espressif/esp-idf](https://github.com/espressif/esp-idf)

This firmware is built using the ESP-IDF (Espressif IoT Development Framework), the official development framework for ESP32 series chips. ESP-IDF provides:

- FreeRTOS-based real-time operating system
- WiFi and Bluetooth drivers
- HTTP server with WebSocket support
- Non-volatile storage (NVS) for configuration

### Apache NimBLE

**Repository:** [https://github.com/apache/mynewt-nimble](https://github.com/apache/mynewt-nimble)

The Bluetooth Low Energy stack used in this project is NimBLE, an open source BLE stack. It provides:

- BLE peripheral role for FTMS server
- BLE central role for heart rate client
- GATT server and client implementations

### cJSON

**Repository:** [https://github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)

JSON parsing and generation is handled by cJSON, a lightweight JSON parser in ANSI C.

### Heart for Bluetooth

**Website:** [https://sites.google.com/view/heartforbluetooth](https://sites.google.com/view/heartforbluetooth)

The recommended Android/Wear OS app for broadcasting heart rate data to the ESP32 rowing monitor.

## Standards & Specifications

### Bluetooth FTMS

This project implements the Bluetooth Fitness Machine Service (FTMS) specification for the Rower Data characteristic. This enables compatibility with fitness apps like:

- Kinomap
- EXR
- MyHomeFit
- And many others

### Concept2 Physics Model

The power and distance calculations use the Concept2-style formula with the boat drag constant of 2.80, making metrics comparable to Concept2 ergometers.

## Community

Thanks to the rowing and fitness hacking communities for sharing knowledge about:

- Reed switch sensor interfacing
- Flywheel physics and calibration
- BLE fitness device protocols

## License

This project is released under the MIT License. See [LICENSE](../LICENSE) for details.

---

*If you believe your work should be acknowledged here, please open an issue or pull request.*
