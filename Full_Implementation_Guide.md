# ESP32 Rowing Monitor - Complete Implementation Guide

## Document Metadata

```yaml
document_type: implementation_guide
version: 2.0.0
created: 2026-01-29
updated: 2026-01-29
target_audience: LLM_and_developers
project_name: ESP32 Rowing Monitor with BLE HR and Health Connect Integration
framework: ESP-IDF (v6.0+)
```

---

## 1. Project Overview

### 1.1 Summary

This project creates a self-contained rowing machine fitness tracking system using three components:

1. **ESP32-S3 Microcontroller**: Acts as the central hub - rowing monitor, BLE heart rate receiver, web server, BLE FTMS, and session storage
2. **Android Watch (Wear OS or Tizen)**: Provides real-time heart rate data via BLE using the "Heart for Bluetooth" app
3. **Android Phone**: Companion app for session management and Health Connect sync (see [ESP32RowingMachineCompanionApp](https://github.com/j0b333/ESP32RowingMachineCompanionApp))

### 1.2 Key Features

- No PC required
- No cloud services required
- No internet required (except initial NTP time sync)
- Bluetooth FTMS compatible (Kinomap, EXR, MyHomeFit, etc.)
- BLE Heart Rate client - receives heart rate from BLE heart rate monitors
- Real-time web interface with WebSocket streaming
- Stores workout sessions with heart rate data
- Sessions persist across ESP32 reboots
- Automatic sync to Samsung Health via Health Connect (using companion app)
- Real-time heart rate display during workout
- Complete rowing metrics: distance, strokes, power, pace, calories

### 1.3 Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          ANDROID WATCH                                       │
│                    (Heart for Bluetooth App)                                 │
│                                                                              │
│    Reads HR sensor → Broadcasts via Bluetooth Low Energy (BLE)              │
│    Standard Heart Rate Service (0x180D)                                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Bluetooth Low Energy
                                    │ BLE GATT Client Connection
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ESP32-S3                                        │
│              Accessible at http://192.168.4.1 (AP mode)                     │
│              or http://rowing.local (STA mode with mDNS)                    │
│                                                                              │
│  Components:                                                                 │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  BLE HR Client  │  │  Rowing Monitor │  │  Session Storage (NVS/SPIFFS)│  │
│  │  - Scans for HR │  │  - Reed switch  │  │  - Multiple sessions        │  │
│  │  - Subscribes   │  │  - Physics calc │  │  - Persists on reboot       │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
│  ┌─────────────────┐  ┌─────────────────────────────────────────────────────┐│
│  │  BLE FTMS       │  │  Web Server (Port 80) + WebSocket (/ws)            ││
│  │  - Fitness apps │  │  - GET /api/status, /api/metrics, /api/sessions    ││
│  │  - Real-time    │  │  - POST /hr, /workout/start, /workout/stop         ││
│  └─────────────────┘  └─────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ WiFi HTTP GET/POST
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           ANDROID APP                                        │
│              (ESP32RowingMachineCompanionApp)                               │
│                                                                              │
│  Features:                                                                   │
│  - Lists all sessions from ESP32                                            │
│  - Syncs individual or all sessions to Health Connect                       │
│  - Marks sessions as synced on ESP32                                        │
│  - Health Connect auto-syncs to Samsung Health                              │
│                                                                              │
│  See: https://github.com/j0b333/ESP32RowingMachineCompanionApp              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.4 Data Flow Sequence

```
1. User powers on ESP32 (creates WiFi AP or connects to existing network)
2. User starts workout (via web interface or Android app)
3. ESP32 scans for and connects to BLE heart rate monitor (e.g., Heart for Bluetooth on watch)
4. User begins rowing
5. ESP32 reed switches detect flywheel rotation and seat movement
6. ESP32 calculates rowing metrics in real-time (physics-based)
7. Heart rate data received via BLE Heart Rate Service subscription
8. ESP32 stores HR samples in memory buffer
9. Metrics broadcast via WebSocket to web clients and BLE FTMS to fitness apps
10. User stops workout
11. ESP32 saves complete session to NVS flash storage
12. User opens ESP32RowingMachineCompanionApp on their phone
13. App fetches session list from ESP32
14. User taps "Sync" on a session
15. App fetches full session data including HR samples
16. App writes ExerciseSessionRecord + HeartRateRecords to Health Connect
17. App marks session as synced on ESP32
18. Health Connect automatically syncs to Samsung Health
```

---

## 2. Hardware Requirements

### 2.1 Bill of Materials

| Item | Specification | Quantity | Approximate Cost | Notes |
|------|---------------|----------|------------------|-------|
| ESP32-S3 DevKitC-1 | N16R8 (16MB Flash, 8MB PSRAM) | 1 | $10-20 USD | Recommended for full features |
| Reed Switch | Normally Open, through-hole | 2 | $2 USD | Already in Crivit rowing machine |
| Jumper Wires | Male-to-female | 4 | $1 USD | For sensor connection |
| USB-C Cable | For power and programming | 1 | $2 USD | |

**Total Hardware Cost: ~$15-25 USD** (excluding watch and phone already owned)

### 2.2 Existing Hardware Required

| Item | Requirement |
|------|-------------|
| Android/Wear OS Watch | Any watch compatible with "Heart for Bluetooth" app |
| Android Phone | Android 9+ with Health Connect support (Android 14+ has it built-in) |
| WiFi Router | 2.4GHz network (ESP32 does not support 5GHz) - Optional if using AP mode |
| Rowing Machine | Crivit or similar with flywheel and seat reed switches |

### 2.3 Wiring Diagram

```
ESP32-S3 DevKitC Pinout:
╔════════════════════════════════════════════════╗
║  Power:                                        ║
║    USB-C → ESP32 (provides power)              ║
║                                                ║
║  Sensor Inputs (direct reed switch connection):║
║    GPIO 15 ← Flywheel reed switch (white wire) ║
║    GPIO 16 ← Seat position reed switch (red)   ║
║    GND      ← Combined sensor grounds          ║
╚════════════════════════════════════════════════╝

Reed Switch Connections:
- Flywheel reed switch   → GPIO 15 + GND
- Seat position switch   → GPIO 16 + GND  
- Combine all grounds    → ESP32 GND

Note: Internal pull-ups are used. No external power needed.
```

---

## 3. Software Architecture

### 3.1 Component Overview

| Component | Language/Framework | Purpose |
|-----------|-------------------|---------|
| ESP32 Firmware | C / ESP-IDF 6.0+ | Core rowing monitor, HR receiver, web server, BLE FTMS |
| Galaxy Watch App | Tizen (pre-existing) | HeartRateToWeb app from Galaxy Store |
| Android App | Kotlin / Jetpack Compose | Session management and Health Connect sync |

### 3.2 ESP-IDF Module Structure

```
main/
├── main.c                  # Application entry point
├── app_config.h            # Configuration constants
├── sensor_manager.c/h      # GPIO interrupt handling with debouncing
├── rowing_physics.c/h      # Core data structures & physics engine
├── stroke_detector.c/h     # Stroke phase detection algorithm
├── metrics_calculator.c/h  # High-level metrics aggregation
├── ble_ftms_server.c/h     # Bluetooth FTMS service implementation
├── ble_hr_client.c/h       # BLE Heart Rate client (connects to HR monitors)
├── wifi_manager.c/h        # WiFi AP/STA management
├── web_server.c/h          # HTTP server with WebSocket support
├── hr_receiver.c/h         # Heart rate storage and management
├── config_manager.c/h      # NVS persistent storage
├── session_manager.c/h     # Session tracking and history
├── utils.c/h               # Utility functions
└── web_content/            # Embedded HTML/CSS/JS files
    ├── index.html
    ├── style.css
    ├── app.js
    └── favicon.ico

components/
└── cJSON/                  # JSON parsing library (managed component)
```

### 3.3 ESP-IDF Dependencies

The project uses ESP-IDF managed components. See `main/idf_component.yml`:

```yaml
dependencies:
  idf:
    version: ">=6.0.0"
  espressif/cJSON:
    version: "*"
```

Required ESP-IDF components (in CMakeLists.txt):
- `esp_driver_gpio` - GPIO driver
- `esp_timer` - High-resolution timer
- `nvs_flash` - Non-volatile storage
- `esp_wifi` - WiFi driver
- `esp_http_server` - HTTP server with WebSocket
- `esp_event` - Event loop
- `bt` - Bluetooth (NimBLE stack)
- `esp_netif` - Network interface
- `cJSON` - JSON parsing

---

## 4. ESP32 Firmware Implementation

### 4.1 Configuration Constants (app_config.h)

```c
// ============== VERSION INFORMATION ==============
#define APP_VERSION_MAJOR       1
#define APP_VERSION_MINOR       0
#define APP_VERSION_PATCH       0
#define APP_VERSION_STRING      "1.0.0"

// ============== GPIO PIN ASSIGNMENTS ==============
#define GPIO_FLYWHEEL_SENSOR    15      // White wire - flywheel reed switch
#define GPIO_SEAT_SENSOR        16      // Red wire - seat position reed switch

// ============== SENSOR TIMING ==============
#define FLYWHEEL_DEBOUNCE_US    10000   // 10ms debounce
#define SEAT_DEBOUNCE_US        50000   // 50ms debounce
#define IDLE_TIMEOUT_MS         5000    // 5 seconds without pulses = idle

// ============== PHYSICS CONSTANTS ==============
#define DEFAULT_MOMENT_OF_INERTIA   0.101f      // kg⋅m²
#define DEFAULT_DRAG_COEFFICIENT    0.0001f     // Initial estimate

// ============== NETWORK SETTINGS ==============
#define WIFI_AP_SSID_DEFAULT        "CrivitRower"
#define WIFI_AP_PASS_DEFAULT        "rowing123"
#define WEB_SERVER_PORT             80
#define WS_BROADCAST_INTERVAL_MS    200     // WebSocket update rate

// ============== BLE SETTINGS ==============
#define BLE_DEVICE_NAME_DEFAULT     "Crivit Rower"
#define BLE_NOTIFY_INTERVAL_MS      500     // BLE FTMS notify rate

// ============== HEART RATE ==============
#define HR_STALE_TIMEOUT_MS         5000    // HR considered stale after 5s
#define MAX_HR_SAMPLES              7200    // Max HR samples (2 hours at 1Hz)
```

### 4.2 Core Data Structures (rowing_physics.h)

```c
/**
 * Stroke phase enumeration
 */
typedef enum {
    STROKE_PHASE_IDLE = 0,      // No activity detected
    STROKE_PHASE_DRIVE,         // Pulling phase (power application)
    STROKE_PHASE_RECOVERY       // Return phase (flywheel coasting)
} stroke_phase_t;

/**
 * Main rowing metrics structure
 */
typedef struct {
    // Timing
    int64_t session_start_time_us;
    int64_t last_update_time_us;
    uint32_t elapsed_time_ms;
    
    // Raw Sensor Data
    volatile uint32_t flywheel_pulse_count;
    volatile int64_t last_flywheel_time_us;
    int64_t prev_flywheel_time_us;
    
    // Flywheel Physics
    float angular_velocity_rad_s;
    float angular_acceleration_rad_s2;
    
    // Drag Model
    float drag_coefficient;
    float moment_of_inertia;
    float drag_factor;
    
    // Stroke Detection
    stroke_phase_t current_phase;
    uint32_t stroke_count;
    float stroke_rate_spm;
    
    // Power & Energy
    float instantaneous_power_watts;
    float average_power_watts;
    float total_work_joules;
    
    // Distance & Pace
    float total_distance_meters;
    float instantaneous_pace_sec_500m;
    float average_pace_sec_500m;
    
    // Calories
    uint32_t total_calories;
    
    // Flags
    bool is_active;
    bool calibration_complete;
    bool valid_data;
} rowing_metrics_t;

/**
 * Configuration structure (stored in NVS)
 */
typedef struct {
    float moment_of_inertia;
    float initial_drag_coefficient;
    float distance_calibration_factor;
    float user_weight_kg;
    char wifi_ssid[32];
    char wifi_password[64];
    char device_name[32];
    bool wifi_enabled;
    bool ble_enabled;
    bool show_power;
    bool show_calories;
    char units[8];  // "metric" or "imperial"
} config_t;

/**
 * Session history entry
 */
typedef struct {
    uint32_t session_id;
    int64_t start_timestamp;
    uint32_t duration_seconds;
    float total_distance_meters;
    float average_pace_sec_500m;
    float average_power_watts;
    uint32_t stroke_count;
    uint32_t total_calories;
    float drag_factor;
    bool synced;                    // Has been synced to Health Connect
    uint16_t hr_sample_count;       // Number of HR samples stored
    uint8_t avg_heart_rate;         // Average HR during session
    uint8_t max_heart_rate;         // Maximum HR during session
} session_record_t;

/**
 * Heart rate sample structure
 */
typedef struct {
    int64_t timestamp_ms;           // Unix timestamp in milliseconds
    uint8_t bpm;                    // Heart rate 0-255
} hr_sample_t;
```

### 4.3 REST API Endpoints

The ESP32 exposes the following HTTP endpoints:

#### Status and Metrics

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Device status and current workout info |
| `/api/metrics` | GET | Current rowing metrics as JSON |
| `/api/config` | GET | Get current configuration |
| `/api/config` | POST | Update configuration |
| `/api/reset` | POST | Reset current session metrics |

#### Heart Rate (HeartRateToWeb Compatible)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/hr` | POST | Receive heart rate from watch (body: "75" or ?bpm=75) |
| `/hr` | GET | Get current heart rate (returns "0" if stale) |

#### Workout Control

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/workout/start` | POST | Start a new workout session |
| `/workout/stop` | POST | Stop current workout and save session |
| `/live` | GET | Get live workout data |

#### Session Management

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/sessions` | GET | List all stored sessions |
| `/api/sessions/{id}` | GET | Get session details with HR samples |
| `/api/sessions/{id}/synced` | POST | Mark session as synced |
| `/api/sessions/{id}` | DELETE | Delete a session |

#### WebSocket

| Endpoint | Protocol | Description |
|----------|----------|-------------|
| `/ws` | WebSocket | Real-time metrics streaming (200ms interval) |

### 4.4 API Response Examples

#### GET /api/status
```json
{
    "version": "1.0.0",
    "device": "Crivit Rowing Monitor",
    "online": true,
    "workoutInProgress": false,
    "sessionCount": 3,
    "currentHeartRate": 72,
    "freeHeap": 180000,
    "uptime": 3600,
    "bleConnected": true,
    "wsClients": 1
}
```

#### GET /api/metrics
```json
{
    "active": true,
    "elapsed_ms": 1234567,
    "time": "20:34.5",
    "distance": 4500.5,
    "pace": 120.5,
    "pace_str": "2:00.5",
    "power": 185.3,
    "spm": 24.5,
    "strokes": 502,
    "calories": 350,
    "drag": 115.2,
    "phase": "drive"
}
```

#### GET /api/sessions
```json
{
    "sessions": [
        {
            "id": 3,
            "startTime": 1706500000000,
            "duration": 1800,
            "distance": 6000.0,
            "strokes": 720,
            "calories": 450,
            "avgPower": 165.5,
            "avgPace": 118.5,
            "avgHeartRate": 145,
            "maxHeartRate": 172,
            "synced": false,
            "hrSampleCount": 1800
        }
    ]
}
```

#### POST /hr (HeartRateToWeb format)
Request body: `75` (plain text) or URL params: `?bpm=75`
Response: `OK` (200) or `Invalid HR value` (400)

---

## 5. Building the ESP32 Firmware

### 5.1 Prerequisites

1. Install [ESP-IDF v6.0+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
2. Set up the ESP-IDF environment

**Note:** This project requires ESP-IDF 6.0 or later. It uses the new modular driver components and managed component system.

### 5.2 Build Commands

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

### 5.3 Key sdkconfig Options

The `sdkconfig.defaults` file includes:

```ini
# Target: ESP32-S3
CONFIG_IDF_TARGET="esp32s3"

# Flash: 16MB QIO
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y

# PSRAM: 8MB Octal
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# Bluetooth: NimBLE peripheral only
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y

# HTTP Server with WebSocket
CONFIG_HTTPD_WS_SUPPORT=y
```

---

## 6. Heart Rate Monitor Setup (Heart for Bluetooth)

The ESP32 acts as a BLE client and connects to standard BLE Heart Rate monitors. The recommended app for Android/Wear OS watches is "Heart for Bluetooth".

### 6.1 About Heart for Bluetooth

Heart for Bluetooth is an Android watch app that broadcasts your heart rate over Bluetooth Low Energy (BLE) using the standard Heart Rate Service (UUID 0x180D). This allows any BLE-compatible device, including the ESP32, to receive heart rate data.

**Key Information:**
- Uses Bluetooth Low Energy (BLE), not Bluetooth Classic
- Compatible with all modern computers and monitors
- Standard Heart Rate Service for maximum compatibility

**Download:** https://sites.google.com/view/heartforbluetooth

### 6.2 Installation and Setup

#### Installation Steps

1. Install "Heart for Bluetooth" on your Android/Wear OS watch from Google Play Store
2. Grant heart rate sensor permissions when prompted
3. The app is now ready to broadcast your heart rate

#### Pairing with ESP32

1. Power on the ESP32 rowing monitor
2. Start Heart for Bluetooth on your watch
3. Navigate to the **[Activity]** screen - your heart rate will start broadcasting
4. You have **120 seconds** to pair with the ESP32
5. If you need more time, restart the Beacon on the **[Connection]** screen
6. The ESP32 will automatically scan, connect, and subscribe to heart rate data

### 6.3 Connection Verification

The **[Connection]** screen in Heart for Bluetooth shows two important values:

| Value | Description |
|-------|-------------|
| **Connected devices** | Number of BLE connections established |
| **Subscribed devices** | Number of devices receiving heart rate data |

**For successful heart rate transmission, both values must be at least 1.** The ESP32 both connects AND subscribes to receive heart rate updates.

### 6.4 Troubleshooting Connection Issues

A common issue is when a device connects but does not subscribe for heart rate data:

1. Ensure the ESP32 is powered on and initialized
2. Check that BLE is enabled in the ESP32 configuration
3. Restart the Heart for Bluetooth beacon on the watch
4. Restart the ESP32 if needed

See the **[Troubleshooting]** screen in Heart for Bluetooth for additional guidance.

### 6.5 Compatible Watches

| Watch Type | Compatibility | Notes |
|------------|--------------|-------|
| Wear OS watches | ✅ Full | Galaxy Watch 4/5/6/7, Pixel Watch, etc. |
| Samsung Galaxy Watch (Tizen) | ✅ Full | Galaxy Watch 3 and earlier |
| Other Android watches | ✅ Full | Any watch supporting the app |

### 6.6 Alternative: HTTP-based Heart Rate (Legacy)

The ESP32 also supports receiving heart rate via HTTP POST for compatibility with other apps like HeartRateToWeb. See the `/hr` endpoint documentation in Section 4.3.

---

## 7. Android Companion App

### 7.1 Overview

The Android companion app is maintained in a **separate repository** to keep the ESP32 firmware repository focused on the embedded system.

**Repository:** [ESP32RowingMachineCompanionApp](https://github.com/j0b333/ESP32RowingMachineCompanionApp)

### 7.2 Features

The companion app provides:
- Session list view with workout details
- Individual or bulk session sync to Health Connect
- Automatic sync status tracking
- Health Connect integration (syncs to Samsung Health, Google Fit, etc.)

### 7.3 Installation

1. Download the latest release from the companion app repository
2. Install on your Android phone (Android 9+)
3. Grant Health Connect permissions when prompted
4. Connect to the same WiFi network as your ESP32 (or connect to ESP32's access point)

For detailed setup instructions, API documentation, and source code, visit the companion app repository.

---

## 8. Usage Guide

### 8.1 First Boot

1. Power on the ESP32 with the rowing machine connected
2. The device creates a WiFi access point:
   - **SSID:** `CrivitRower`
   - **Password:** `rowing123`
3. Connect your phone/tablet to this network
4. Open a browser and go to `http://192.168.4.1`

### 8.2 Web Interface

The web interface displays:
- **Distance** - Total distance rowed (meters)
- **Time** - Elapsed workout time
- **Pace /500m** - Current pace (time per 500 meters)
- **Power** - Instantaneous power output (watts)
- **Stroke Rate** - Strokes per minute (SPM)
- **Calories** - Estimated calories burned
- **Heart Rate** - Current HR from BLE heart rate monitor (if connected)

### 8.3 Bluetooth Connection

1. Open a compatible fitness app (Kinomap, EXR, MyHomeFit, etc.)
2. Search for Bluetooth devices
3. Connect to "Crivit Rower"
4. The app will receive real-time rowing data via FTMS protocol

### 8.4 Heart Rate Monitor Connection

1. Power on the ESP32 rowing monitor
2. Start "Heart for Bluetooth" on your watch
3. The ESP32 will automatically scan for and connect to the heart rate monitor
4. Heart rate will be displayed on the web interface when connected
5. Heart rate data is included in workout sessions

### 8.5 Syncing to Samsung Health

1. Ensure ESP32 and phone are on same network
2. Open ESP32RowingMachineCompanionApp
3. Enter ESP32 address (or use auto-discover)
4. View list of workout sessions
5. Tap "Sync" on individual sessions or "Sync All"
6. Sessions appear in Samsung Health automatically

---

## 9. Troubleshooting

### 9.1 No Sensor Readings
- Check reed switch wiring connections
- Verify GPIO pins match configuration (15 for flywheel, 16 for seat)
- Check serial monitor for debug output

### 9.2 WiFi Connection Issues
- Ensure device is powered and initialized (check serial output)
- Verify correct SSID/password
- Try connecting via IP address instead of mDNS
- Move closer to the ESP32

### 9.3 BLE FTMS Not Visible
- Ensure Bluetooth is enabled on your phone/tablet
- Check if another device is already connected (max 3 connections)
- Restart the ESP32

### 9.4 Heart Rate Not Updating
- Ensure "Heart for Bluetooth" is running on the watch
- Check the [Connection] screen shows "Subscribed devices: 1"
- Restart the Heart for Bluetooth beacon on the watch
- Check serial monitor for BLE HR client connection status
- Ensure watch has granted heart rate permission
- If using HTTP method, check `/hr` endpoint response in browser

### 9.5 Inaccurate Metrics
- Row for 50+ strokes to allow drag auto-calibration
- Verify moment of inertia setting for your machine
- Check reed switch alignment and timing

---

## 10. License

This project is open source. See LICENSE file for details.

## 11. Acknowledgments

- [OpenRowingMonitor](https://github.com/JaapvanEkwortel/openrowingmonitor) for physics algorithms
- [ESP-IDF](https://github.com/espressif/esp-idf) framework
- [NimBLE](https://github.com/apache/mynewt-nimble) Bluetooth stack
- [Heart for Bluetooth](https://sites.google.com/view/heartforbluetooth) - Android watch HR broadcasting app
