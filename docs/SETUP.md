# ESP32 Rowing Monitor - Setup Guide

This guide covers hardware setup, firmware building, and device configuration.

## Hardware Requirements

### Required Components

| Item | Specification | Notes |
|------|---------------|-------|
| ESP32-S3 DevKitC-1 | N16R8 (16MB Flash, 8MB PSRAM) | Or compatible ESP32-S3 board |
| Rowing Machine | With reed switches | Crivit or similar |
| USB-C Cable | Data capable | For programming and power |

### Optional Components

| Item | Purpose |
|------|---------|
| Android/Wear OS Watch | Heart rate monitoring via BLE |
| BLE Heart Rate Strap | Alternative HR monitoring |
| Android Phone | Companion app for Health Connect sync |

## Hardware Wiring

The ESP32 connects directly to the rowing machine's reed switches. Internal pull-up resistors are used, so no external components are needed.

```
ESP32-S3 DevKitC Pinout:
╔════════════════════════════════════════════════╗
║  Power:                                        ║
║    USB-C → ESP32 (provides power)              ║
║                                                ║
║  Sensor Inputs:                                ║
║    GPIO 15 ← Flywheel reed switch              ║
║    GPIO 16 ← Seat position reed switch         ║
║    GND      ← Combined sensor grounds          ║
╚════════════════════════════════════════════════╝
```

### Reed Switch Connection

1. Locate the flywheel reed switch (usually white wire)
2. Locate the seat position switch (usually red wire)
3. Connect flywheel switch to GPIO 15 and GND
4. Connect seat switch to GPIO 16 and GND
5. Combine all ground wires to a single ESP32 GND pin

**Note:** Reed switches are normally open. They close briefly when a magnet passes.

## Building the Firmware

### Prerequisites

1. Install [ESP-IDF v6.0+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
2. Set up the ESP-IDF environment variables

**Important:** This project requires ESP-IDF 6.0 or later for the modular driver components.

### Build Commands

```bash
# Clone the repository
git clone https://github.com/j0b333/ESP32RowingMachine.git
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

Replace `/dev/ttyUSB0` with your serial port (e.g., `COM3` on Windows).

### Clean Build

If you encounter build errors after updating:

```bash
idf.py fullclean
rm sdkconfig
idf.py set-target esp32s3
idf.py build
```

## First Boot

1. Power on the ESP32 (connect USB-C)
2. The device creates a WiFi access point:
   - **SSID:** `CrivitRower`
   - **Password:** `rowing123`
3. Connect your phone/tablet to this network
4. Open a browser and navigate to `http://192.168.4.1`

## Web Interface

The web interface displays real-time metrics:

| Metric | Description |
|--------|-------------|
| Distance | Total distance rowed (meters) |
| Time | Elapsed workout time |
| Pace /500m | Time per 500 meters |
| Power | Instantaneous power (watts) |
| Stroke Rate | Strokes per minute (SPM) |
| Calories | Estimated energy burned |
| Heart Rate | BPM from connected monitor |

### Settings

Access settings via the ⚙️ button:

- **Weight** - Your weight for calorie calculation
- **Units** - Metric or Imperial
- **Display** - Show/hide power and calories
- **Auto-pause** - Pause workout after inactivity

## Heart Rate Monitor Setup (Optional)

The BLE Heart Rate client feature is **disabled by default** to reduce build complexity.

### Enabling BLE Heart Rate Client

1. Edit `main/app_config.h`:
   ```c
   #define BLE_HR_CLIENT_ENABLED           1
   ```

2. Run `idf.py menuconfig` and enable:
   - `Component config → Bluetooth → NimBLE Options → Roles`:
     - ✅ Enable BLE Central role
     - ✅ Enable BLE Observer role
     - ✅ Enable BLE GATT Client support

3. Clean rebuild:
   ```bash
   idf.py fullclean
   idf.py build
   ```

### Using Heart for Bluetooth (Recommended)

1. Install "Heart for Bluetooth" on your Android/Wear OS watch
2. Start the app and go to the Activity screen
3. Power on the ESP32 rowing monitor
4. The ESP32 automatically scans, connects, and subscribes
5. Heart rate appears on the web interface

**Pairing Window:** You have 120 seconds to pair. Restart the beacon on the watch if needed.

See [Heart for Bluetooth](https://sites.google.com/view/heartforbluetooth) for more information.

## Bluetooth FTMS Connection

For use with fitness apps (Kinomap, EXR, MyHomeFit, etc.):

1. Start a workout on the ESP32 web interface
2. Open your fitness app
3. Search for Bluetooth devices
4. Connect to "Crivit Rower"
5. Rowing data streams via FTMS protocol

## Calibration

### Automatic Drag Calibration

The drag coefficient auto-calibrates during rowing. Row for 50+ strokes to stabilize the drag factor reading.

### Moment of Inertia Calibration

For accurate power readings, calibrate the flywheel inertia:

1. Go to Settings → Click "Calibrate Inertia"
2. Give the flywheel one strong pull
3. Let it coast to a complete stop (don't touch it!)
4. The system calculates and saves the moment of inertia

See [Physics Model](PHYSICS_MODEL.md) for calibration details.

## Companion App

Sync workouts to Samsung Health / Google Fit:

1. Install [ESP32RowingMachineCompanionApp](https://github.com/j0b333/ESP32RowingMachineCompanionApp)
2. Connect to the same network as the ESP32
3. View and sync workout sessions
4. Sessions appear in Samsung Health via Health Connect

## Troubleshooting

### No Sensor Readings

- Check wiring connections
- Verify GPIO pins (15 for flywheel, 16 for seat)
- Check serial monitor for debug output

### WiFi Connection Issues

- Ensure device is powered (check LED)
- Verify SSID and password
- Try connecting via IP: `192.168.4.1`
- Move closer to the ESP32

### BLE Not Visible

- Ensure Bluetooth is enabled on your phone
- Check if another device is already connected (max 3)
- Restart the ESP32

### Inaccurate Metrics

- Row for 50+ strokes to calibrate drag
- Run moment of inertia calibration
- Check reed switch alignment

### Build Errors

- Ensure ESP-IDF v6.0+ is installed
- Run clean build: `idf.py fullclean`
- Delete `sdkconfig` and rebuild

## Advanced Configuration

Edit `main/app_config.h` for advanced settings:

| Setting | Default | Description |
|---------|---------|-------------|
| `GPIO_FLYWHEEL_SENSOR` | 15 | Flywheel GPIO pin |
| `GPIO_SEAT_SENSOR` | 16 | Seat sensor GPIO pin |
| `DEFAULT_MOMENT_OF_INERTIA` | 0.101 | Flywheel inertia (kg⋅m²) |
| `WIFI_AP_SSID_DEFAULT` | "CrivitRower" | WiFi network name |
| `WIFI_AP_PASS_DEFAULT` | "rowing123" | WiFi password |
| `BLE_DEVICE_NAME_DEFAULT` | "Crivit Rower" | Bluetooth name |
