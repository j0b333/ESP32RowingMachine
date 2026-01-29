# ESP32 Rowing Monitor

A smart rowing machine monitor firmware for ESP32-S3 that transforms a Crivit-branded rowing machine into a connected fitness device with Bluetooth FTMS support and a web-based interface.

## Features

- **Bluetooth Low Energy FTMS** - Compatible with fitness apps like Kinomap, EXR, MyHomeFit, and more
- **BLE Heart Rate Client** - Connects to BLE heart rate monitors (e.g., "Heart for Bluetooth" watch app)
- **Real-time Web Interface** - Accessible via smartphone/tablet browser over WiFi
- **Physics-based Metrics** - Accurate power, pace, and distance calculations
- **Automatic Drag Calibration** - Self-adjusting drag coefficient for accurate measurements
- **Session Tracking** - Stores workout history with heart rate data in non-volatile storage

## Companion App

For syncing workouts to Samsung Health/Google Fit via Health Connect, see the companion Android app:
**[ESP32RowingMachineCompanionApp](https://github.com/j0b333/ESP32RowingMachineCompanionApp)**

## Heart Rate Monitor Setup

This firmware supports receiving heart rate data via Bluetooth Low Energy from standard heart rate monitors.

### Recommended: Heart for Bluetooth

1. Install "Heart for Bluetooth" on your Android/Wear OS watch
2. Start the app and navigate to the Activity screen
3. The ESP32 will automatically scan, connect, and subscribe to heart rate data
4. Heart rate is displayed on the web interface during workouts

For more details, see the [Full Implementation Guide](Full_Implementation_Guide.md).

## Hardware Requirements

### Target Hardware
- **ESP32-S3 DevKitC-1 N16R8** (16MB Flash, 8MB PSRAM)
- Or compatible ESP32-S3 development board

### Sensor Connections

The ESP32 connects directly to the rowing machine's reed switches using internal pull-up resistors. No external power supply is needed.

```
ESP32-S3 DevKitC Pinout:
╔════════════════════════════════════════════════╗
║  Power:                                        ║
║    USB-C → ESP32 (provides power)              ║
║                                                ║
║  Sensor Inputs (direct reed switch connection):║
║    GPIO 15 ← Flywheel reed switch              ║
║    GPIO 16 ← Seat position reed switch         ║
║    GND      ← Combined sensor grounds          ║
╚════════════════════════════════════════════════╝
```

### Rowing Machine Wiring

```
Reed Switch Connections:
- Flywheel reed switch   → GPIO 15 + GND
- Seat position switch   → GPIO 16 + GND
- Combine all grounds    → ESP32 GND

Note: Internal pull-ups are used. No external power needed.
```

## Building the Firmware

### Prerequisites

1. Install [ESP-IDF v6.0+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
2. Set up the ESP-IDF environment

**Note:** This project requires ESP-IDF 6.0 or later. It uses the new modular driver components and managed component system introduced in ESP-IDF 6.0.

### Build Commands

```bash
# Clone the repository
git clone https://github.com/yourusername/ESP32RowingMachine.git
cd ESP32RowingMachine

# Set target to ESP32-S3
idf.py set-target esp32s3

# Configure (optional - defaults are in sdkconfig.defaults)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

### Clean Build

If you encounter build errors after updating the code (especially BLE-related errors), perform a clean build:

```bash
# Remove build directory and sdkconfig
idf.py fullclean
rm sdkconfig

# Rebuild from scratch
idf.py set-target esp32s3
idf.py build
```

## Usage

### First Boot

1. Power on the ESP32 with the rowing machine connected
2. The device creates a WiFi access point:
   - **SSID:** `CrivitRower`
   - **Password:** `rowing123`
3. Connect your phone/tablet to this network
4. Open a browser and go to `http://192.168.4.1`

### Web Interface

The web interface displays:
- **Distance** - Total distance rowed (meters)
- **Time** - Elapsed workout time
- **Pace /500m** - Current pace (time per 500 meters)
- **Power** - Instantaneous power output (watts)
- **Stroke Rate** - Strokes per minute (SPM)
- **Calories** - Estimated calories burned

### Bluetooth Connection

1. Open a compatible fitness app (Kinomap, EXR, MyHomeFit, etc.)
2. Search for Bluetooth devices
3. Connect to "Crivit Rower"
4. The app will receive real-time rowing data via FTMS protocol

## Configuration

### Web Settings
Access settings via the "Settings" button on the web interface:
- **Weight** - User weight for calorie calculation
- **Units** - Metric or Imperial
- **Display options** - Show/hide power and calories

### Advanced Configuration
Modify `main/app_config.h` for advanced settings:
- Sensor GPIO pins
- Physics parameters (moment of inertia, drag coefficient)
- Detection thresholds
- Network credentials

## Architecture

### Module Overview

| Module | Description |
|--------|-------------|
| `sensor_manager` | GPIO interrupt handling with debouncing |
| `rowing_physics` | Physics-based calculations (power, distance, pace) |
| `stroke_detector` | Stroke phase detection algorithm |
| `metrics_calculator` | High-level metrics aggregation |
| `ble_ftms_server` | Bluetooth FTMS service implementation |
| `wifi_manager` | WiFi AP/STA management |
| `web_server` | HTTP server with WebSocket support |
| `config_manager` | NVS persistent storage |
| `session_manager` | Session tracking and history |

### Data Flow

```
Sensors → ISR → Event Group → Sensor Task → Physics Engine
                                              ↓
                                    Stroke Detection
                                              ↓
                              ┌───────────────┴───────────────┐
                              ↓                               ↓
                        BLE FTMS                        WebSocket
                    (Fitness Apps)                    (Web Browser)
```

## Physics Model

The firmware uses a physics-based approach for accurate metrics:

### Angular Velocity
```
ω = 2π / Δt
```
Where Δt is the time between consecutive flywheel pulses.

### Power Calculation
```
P = (I × α + k × ω²) × ω
```
Where:
- I = moment of inertia (0.101 kg⋅m² default)
- α = angular acceleration
- k = drag coefficient (auto-calibrated)
- ω = angular velocity

### Drag Calibration
During recovery phases, the drag coefficient is automatically calculated:
```
k = -I × α / ω²
```
Filtered using exponential moving average for stability.

## Troubleshooting

### No sensor readings
- Check wiring connections
- Verify 3.3V power to sensors
- Check GPIO pin assignments in `app_config.h`

### WiFi connection issues
- Ensure device is powered and initialized
- Check for correct SSID/password
- Try moving closer to the ESP32

### BLE not visible
- Ensure Bluetooth is enabled on your phone
- Check if another device is already connected
- Restart the ESP32

### Inaccurate metrics
- Row for 50+ strokes to allow drag calibration
- Verify moment of inertia setting
- Check for sensor debounce issues

## License

This project is open source. See LICENSE file for details.

## Acknowledgments

- [OpenRowingMonitor](https://github.com/Jannuel-Dizon/openrowingmonitor-ESP32S3-ZephyrOS) for physics algorithms
- [ESP-IDF](https://github.com/espressif/esp-idf) framework
- [NimBLE](https://github.com/apache/mynewt-nimble) Bluetooth stack
