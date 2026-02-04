# ESP32 Rowing Monitor - Architecture

This document describes the system architecture, module structure, and data flow of the ESP32 Rowing Monitor firmware.

## System Overview

The ESP32 Rowing Monitor is a real-time embedded system that transforms a basic rowing machine into a connected fitness device. It runs on ESP-IDF (FreeRTOS) with multiple concurrent tasks handling sensors, physics calculations, networking, and Bluetooth.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ESP32-S3                                        │
│              Accessible at http://192.168.4.1 (AP mode)                     │
│              or http://rowing.local (STA mode with mDNS)                    │
│                                                                              │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  BLE HR Client  │  │  Rowing Monitor │  │  Session Storage (NVS)      │  │
│  │  - Scans for HR │  │  - Reed switch  │  │  - Multiple sessions        │  │
│  │  - Subscribes   │  │  - Physics calc │  │  - Persists on reboot       │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
│  ┌─────────────────┐  ┌─────────────────────────────────────────────────────┐│
│  │  BLE FTMS       │  │  Web Server (Port 80) + WebSocket (/ws)            ││
│  │  - Fitness apps │  │  - REST API for sessions and configuration         ││
│  │  - Real-time    │  │  - Real-time metrics streaming                     ││
│  └─────────────────┘  └─────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────────┘
```

## Module Structure

```
main/
├── main.c                  # Application entry point and task initialization
├── app_config.h            # Configuration constants and defaults
│
├── sensor_manager.c/h      # GPIO interrupt handling with debouncing
├── rowing_physics.c/h      # Core physics calculations
├── stroke_detector.c/h     # Stroke phase detection algorithm
├── metrics_calculator.c/h  # High-level metrics aggregation
│
├── ble_ftms_server.c/h     # Bluetooth FTMS service (peripheral role)
├── ble_hr_client.c/h       # BLE Heart Rate client (central role)
├── hr_receiver.c/h         # Heart rate storage and management
│
├── wifi_manager.c/h        # WiFi AP/STA management
├── web_server.c/h          # HTTP server with WebSocket support
├── dns_server.c/h          # Captive portal DNS server
│
├── config_manager.c/h      # NVS persistent storage
├── session_manager.c/h     # Session tracking and history
├── utils.c/h               # Utility functions
│
└── web_content/            # Embedded HTML/CSS/JS files
    ├── index.html
    ├── style.css
    ├── app.js
    └── favicon.ico

components/
└── cJSON/                  # JSON parsing library
```

## Module Descriptions

### Core Modules

#### sensor_manager
Handles low-level GPIO interrupt processing for the flywheel and seat reed switches.
- Configures GPIO pins with internal pull-ups
- Implements hardware debouncing using timestamps
- Triggers event group bits for the sensor task

#### rowing_physics
The physics engine that calculates all rowing metrics.
- Angular velocity from pulse timing
- Power calculation using torque equation
- Drag coefficient auto-calibration
- Distance calculation using Concept2 formula
- Spindown-based moment of inertia calibration

#### stroke_detector
Detects stroke phases (drive/recovery) based on flywheel behavior.
- Drive phase: Angular acceleration above threshold
- Recovery phase: Flywheel coasting (negative acceleration)
- Idle: No activity for timeout period

#### metrics_calculator
Aggregates raw physics data into user-facing metrics.
- Stroke rate calculation
- Average/best pace tracking
- Calorie estimation
- Session statistics

### BLE Modules

#### ble_ftms_server
Implements Bluetooth FTMS (Fitness Machine Service) for rowing data.
- Advertises as "Crivit Rower"
- Rower Data characteristic with standard fields
- Compatible with Kinomap, EXR, MyHomeFit, etc.

#### ble_hr_client
Acts as BLE central to receive heart rate from monitors.
- Scans for Heart Rate Service (0x180D)
- Connects and subscribes to HR notifications
- Supports Heart for Bluetooth and other BLE HR monitors

#### hr_receiver
Manages heart rate data from multiple sources.
- BLE heart rate monitor
- HTTP POST from HeartRateToWeb
- Staleness detection (5 second timeout)

### Network Modules

#### wifi_manager
Manages WiFi in Access Point or Station mode.
- Creates AP: SSID "CrivitRower" with WPA2 password "12345678"
- Can connect to existing WiFi network
- mDNS for `rowing.local` hostname

#### web_server
HTTP server with WebSocket support.
- Serves embedded web UI
- REST API for metrics, sessions, configuration
- WebSocket for real-time streaming at 5 Hz

#### dns_server
Captive portal DNS server for AP mode.
- Redirects all DNS queries to ESP32 IP
- Enables automatic portal popup on mobile devices

### Storage Modules

#### config_manager
Persistent configuration storage using NVS.
- User settings (weight, units, display options)
- Calibration values (moment of inertia, drag)
- Network credentials

#### session_manager
Workout session storage and retrieval.
- Stores session summaries and per-second samples
- Supports multiple sessions (limited by flash size)
- Sync status tracking for companion app

## Data Flow

```
                    ┌─────────────────┐
                    │   Reed Switches │
                    │  (GPIO 15, 16)  │
                    └────────┬────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────┐
│                    ISR Handler                          │
│  - Debounce check                                       │
│  - Set event group bits                                 │
└─────────────────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────┐
│                   Sensor Task                           │
│  - Wait for event bits                                  │
│  - Update raw pulse counts                              │
│  - Calculate angular velocity                           │
└─────────────────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────┐
│                  Physics Engine                         │
│  - Angular acceleration                                 │
│  - Drag calibration (recovery phase)                    │
│  - Power calculation                                    │
│  - Distance (per stroke)                                │
└─────────────────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────┐
│                 Stroke Detector                         │
│  - Phase detection (drive/recovery/idle)                │
│  - Stroke counting                                      │
│  - Stroke rate calculation                              │
└─────────────────────────────────────────────────────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
      ┌───────────┐  ┌───────────┐  ┌───────────┐
      │ BLE FTMS  │  │ WebSocket │  │  Session  │
      │  Server   │  │ Broadcast │  │  Storage  │
      └───────────┘  └───────────┘  └───────────┘
              │              │              │
              ▼              ▼              ▼
       Fitness Apps    Web Browser    Companion App
```

## Task Architecture

The firmware uses FreeRTOS tasks with different priorities:

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| Sensor Task | 10 (High) | 4KB | Process GPIO events, update physics |
| Metrics Task | 5 (Medium) | 4KB | Aggregate metrics, manage sessions |
| BLE Task | 4 (Medium) | 4KB | FTMS notifications, HR scanning |
| Web Task | 3 (Low) | 8KB | HTTP/WebSocket handling |

## Synchronization

- **Metrics Mutex**: Protects `rowing_metrics_t` structure during read/write
- **Event Groups**: Signal sensor events from ISR to task
- **Atomic Operations**: Used for volatile counters (pulse counts)

## Memory Usage

- **Flash**: Firmware ~1MB, Web content ~50KB, Session storage ~500KB
- **RAM**: ~180KB free heap during operation
- **PSRAM**: Available on N16R8 module (8MB) for future expansion

## Configuration

All compile-time configuration is in `app_config.h`:

| Category | Key Settings |
|----------|--------------|
| GPIO | Flywheel (15), Seat (16) pins |
| Timing | Debounce (10ms), Idle timeout (5s) |
| Physics | Moment of inertia, drag coefficient |
| Network | AP SSID/password, server port |
| BLE | Device name, notify interval |

Runtime configuration is stored in NVS and accessible via the REST API.

## Adding New Features

### New REST Endpoint

1. Add handler function in `web_server.c`
2. Register URI in `web_server_start()`
3. Document in `docs/API.md`

### New Metric

1. Add field to `rowing_metrics_t` in `rowing_physics.h`
2. Calculate in appropriate physics function
3. Include in JSON output in `web_server.c`
4. Update WebSocket broadcast

### New Configuration Option

1. Add field to `config_t` in `rowing_physics.h`
2. Add NVS key in `app_config.h`
3. Load/save in `config_manager.c`
4. Expose via REST API in `web_server.c`
