# ESP32 Rowing Monitor - Complete Implementation Guide

## Document Metadata

```yaml
document_type: implementation_guide
version: 2.0.0
created: 2026-01-29
updated: 2026-01-29
target_audience: LLM_and_developers
project_name: ESP32 Rowing Monitor with Galaxy Watch HR and Health Connect Integration
framework: ESP-IDF (v6.0+)
```

---

## 1. Project Overview

### 1.1 Summary

This project creates a self-contained rowing machine fitness tracking system using three components:

1. **ESP32-S3 Microcontroller**: Acts as the central hub - rowing monitor, heart rate receiver, web server, BLE FTMS, and session storage
2. **Samsung Galaxy Watch**: Provides real-time heart rate data via existing HeartRateToWeb Tizen app
3. **Android Phone**: Simple app that fetches sessions from ESP32 and writes to Health Connect (syncs to Samsung Health)

### 1.2 Key Features

- No PC required
- No cloud services required
- No internet required (except initial NTP time sync)
- Bluetooth FTMS compatible (Kinomap, EXR, MyHomeFit, etc.)
- Real-time web interface with WebSocket streaming
- Stores workout sessions with heart rate data
- Sessions persist across ESP32 reboots
- Automatic sync to Samsung Health via Health Connect
- Real-time heart rate display during workout
- Complete rowing metrics: distance, strokes, power, pace, calories

### 1.3 Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          GALAXY WATCH                                        │
│                    (HeartRateToWeb Tizen App)                               │
│                                                                              │
│    Reads HR sensor → HTTP POST /hr every 1 second                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ WiFi HTTP POST
                                    │ http://rowing.local/hr
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ESP32-S3                                        │
│              Accessible at http://192.168.4.1 (AP mode)                     │
│              or http://rowing.local (STA mode with mDNS)                    │
│                                                                              │
│  Components:                                                                 │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  HR Receiver    │  │  Rowing Monitor │  │  Session Storage (NVS/SPIFFS)│  │
│  │  - POST /hr     │  │  - Reed switch  │  │  - Multiple sessions        │  │
│  │  - GET /hr      │  │  - Physics calc │  │  - Persists on reboot       │  │
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
│                                                                              │
│  Features:                                                                   │
│  - Lists all sessions from ESP32                                            │
│  - Syncs individual or all sessions to Health Connect                       │
│  - Marks sessions as synced on ESP32                                        │
│  - Health Connect auto-syncs to Samsung Health                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.4 Data Flow Sequence

```
1. User powers on ESP32 (creates WiFi AP or connects to existing network)
2. User starts workout (via web interface or Android app)
3. User begins rowing
4. ESP32 reed switches detect flywheel rotation and seat movement
5. ESP32 calculates rowing metrics in real-time (physics-based)
6. Galaxy Watch HeartRateToWeb app sends HR to ESP32 every second
7. ESP32 stores HR samples in memory buffer
8. Metrics broadcast via WebSocket to web clients and BLE FTMS to fitness apps
9. User stops workout
10. ESP32 saves complete session to NVS flash storage
11. User opens Android app
12. App fetches session list from ESP32
13. User taps "Sync" on a session
14. App fetches full session data including HR samples
15. App writes ExerciseSessionRecord + HeartRateRecords to Health Connect
16. App marks session as synced on ESP32
17. Health Connect automatically syncs to Samsung Health
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
| Samsung Galaxy Watch | Any model with HeartRateToWeb support |
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
├── wifi_manager.c/h        # WiFi AP/STA management
├── web_server.c/h          # HTTP server with WebSocket support
├── hr_receiver.c/h         # Heart rate HTTP endpoint handler (NEW)
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

## 6. Galaxy Watch Configuration

### 6.1 HeartRateToWeb App Setup

The Galaxy Watch uses the existing HeartRateToWeb Tizen app. No custom development required.

#### Installation Steps

1. Open Galaxy Store on your Samsung Galaxy Watch
2. Search for "HeartRateToWeb" or "Heart Rate to Web"
3. Install the app
4. If not available, sideload from GitHub: https://github.com/loic2665/HeartRateToWeb

#### Configuration Steps

1. Connect Galaxy Watch to the same WiFi as ESP32 (or ESP32's AP)
2. Open HeartRateToWeb app on watch
3. Enter ESP32 address: `192.168.4.1` (AP mode) or `rowing.local` (STA mode)
4. Enter port: `80`
5. Tap "Start" to begin sending heart rate data
6. The app will POST to `http://<address>/hr` every second

### 6.2 Compatible Watches

| Watch Model | Compatibility | Notes |
|-------------|--------------|-------|
| Galaxy Watch 7/6/5/4 | ✅ Full | Wear OS based |
| Galaxy Watch 3/Active 2/Active | ✅ Full | Tizen based |
| Galaxy Watch (Original) | ✅ Full | Tizen based |
| Gear S3/Sport | ✅ Full | Tizen based |

---

## 7. Android App Implementation

### 7.1 Project Setup

The Android app is maintained in a **separate repository** to keep the ESP32 firmware repository focused. See `android-app/` directory for starter files that should be moved to a new repository.

#### Android Studio Project Configuration

```
Project Name: RowingSync
Package Name: com.yourname.rowingsync
Minimum SDK: API 28 (Android 9.0)
Target SDK: API 34 (Android 14)
Language: Kotlin
Build System: Gradle Kotlin DSL
```

### 7.2 Key Dependencies

```kotlin
dependencies {
    // Health Connect
    implementation("androidx.health.connect:connect-client:1.1.0-alpha07")
    
    // Networking
    implementation("com.squareup.retrofit2:retrofit:2.9.0")
    implementation("com.squareup.retrofit2:converter-gson:2.9.0")
    
    // Compose UI
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.compose.material3:material3:1.2.0")
    
    // Coroutines
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}
```

### 7.3 Health Connect Permissions

The app requires these Health Connect permissions:
- `READ_EXERCISE` / `WRITE_EXERCISE`
- `READ_HEART_RATE` / `WRITE_HEART_RATE`
- `READ_DISTANCE` / `WRITE_DISTANCE`
- `READ_TOTAL_CALORIES_BURNED` / `WRITE_TOTAL_CALORIES_BURNED`

### 7.4 Network Security

For local HTTP access to ESP32, configure `network_security_config.xml`:

```xml
<network-security-config>
    <domain-config cleartextTrafficPermitted="true">
        <domain includeSubdomains="true">192.168.0.0/16</domain>
        <domain includeSubdomains="true">10.0.0.0/8</domain>
        <domain includeSubdomains="true">172.16.0.0/12</domain>
    </domain-config>
</network-security-config>
```

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
- **Heart Rate** - Current HR from Galaxy Watch (if connected)

### 8.3 Bluetooth Connection

1. Open a compatible fitness app (Kinomap, EXR, MyHomeFit, etc.)
2. Search for Bluetooth devices
3. Connect to "Crivit Rower"
4. The app will receive real-time rowing data via FTMS protocol

### 8.4 Syncing to Samsung Health

1. Ensure ESP32 and phone are on same network
2. Open RowingSync Android app
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

### 9.3 BLE Not Visible
- Ensure Bluetooth is enabled on your phone/tablet
- Check if another device is already connected (max 3 connections)
- Restart the ESP32

### 9.4 Heart Rate Not Updating
- Verify watch is on same WiFi network as ESP32
- Check HeartRateToWeb configuration (correct IP and port)
- Ensure watch has granted heart rate permission
- Check `/hr` endpoint response in browser

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
- [HeartRateToWeb](https://github.com/loic2665/HeartRateToWeb) Galaxy Watch app
