```markdown name=ESP32_Rowing_Monitor_Complete_Implementation_Guide.md
# ESP32 Rowing Monitor - Complete Implementation Guide

## Document Metadata

```yaml
document_type: implementation_guide
version: 1.0.0
created: 2026-01-29
target_audience: LLM_and_developers
project_name: ESP32 Rowing Monitor with Galaxy Watch HR and Health Connect Integration
```

---

## 1. Project Overview

### 1.1 Summary

This project creates a self-contained rowing machine fitness tracking system using three components:

1. **ESP32 Microcontroller**: Acts as the central hub - rowing monitor, heart rate receiver, web server, and session storage
2. **Samsung Galaxy Watch**: Provides real-time heart rate data via existing HeartRateToWeb Tizen app
3. **Android Phone**: Simple app that fetches sessions from ESP32 and writes to Health Connect (syncs to Samsung Health)

### 1.2 Key Features

- No PC required
- No cloud services required
- No internet required (except initial NTP time sync)
- Stores up to 10 complete workout sessions with full heart rate data
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
│                              ESP32                                           │
│                    Accessible at http://rowing.local                         │
│                                                                              │
│  Components:                                                                 │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  HR Receiver    │  │  Rowing Monitor │  │  Session Storage (LittleFS) │  │
│  │  - POST /hr     │  │  - Hall sensor  │  │  - 10 sessions max          │  │
│  │  - GET /hr      │  │  - Physics calc │  │  - Persists on reboot       │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │  Web Server (Port 80)                                                   ││
│  │  - GET /status, GET /sessions, GET /sessions/{id}                       ││
│  │  - POST /workout/start, POST /workout/stop, POST /sessions/{id}/synced  ││
│  └─────────────────────────────────────────────────────────────────────────┘│
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
1. User starts workout (via ESP32 button or Android app)
2. User begins rowing
3. ESP32 hall sensor detects flywheel rotation
4. ESP32 calculates rowing metrics in real-time
5. Galaxy Watch HeartRateToWeb app sends HR to ESP32 every second
6. ESP32 stores HR samples in memory buffer
7. User stops workout
8. ESP32 saves complete session to LittleFS flash storage
9. User opens Android app
10. App fetches session list from ESP32
11. User taps "Sync" on a session
12. App fetches full session data including HR samples
13. App writes ExerciseSessionRecord + HeartRateRecords to Health Connect
14. App marks session as synced on ESP32
15. Health Connect automatically syncs to Samsung Health
```

---

## 2. Hardware Requirements

### 2.1 Bill of Materials

| Item | Specification | Quantity | Approximate Cost | Notes |
|------|---------------|----------|------------------|-------|
| ESP32 Development Board | ESP32-WROOM-32 or ESP32-S3 | 1 | $5-15 USD | Any ESP32 variant with WiFi works |
| Hall Effect Sensor | A3144 or SS49E | 1 | $0.50-2 USD | Digital output preferred |
| Neodymium Magnets | 6mm x 3mm disc | 4-8 | $2-5 USD | Attach to flywheel |
| Resistor | 10kΩ | 1 | $0.10 USD | Pull-up for hall sensor (optional if using INPUT_PULLUP) |
| Jumper Wires | Male-to-female | 3 | $1 USD | For sensor connection |
| USB Cable | Micro-USB or USB-C | 1 | $2 USD | For power and programming |
| Power Supply | 5V 1A USB adapter | 1 | $3 USD | Or use USB power bank |

**Total Hardware Cost: $13-28 USD** (excluding watch and phone already owned)

### 2.2 Existing Hardware Required

| Item | Requirement |
|------|-------------|
| Samsung Galaxy Watch | Any model with HeartRateToWeb support (Galaxy Watch, Active, Active 2, Gear S2/S3/Sport) |
| Android Phone | Android 9+ with Health Connect support (Android 14+ has it built-in) |
| WiFi Router | 2.4GHz network (ESP32 does not support 5GHz) |
| Rowing Machine | Any with accessible flywheel for magnet attachment |

### 2.3 Wiring Diagram

```
ESP32 Pinout:
                    ┌─────────────────────┐
                    │       ESP32         │
                    │                     │
    Hall Sensor ────┤ GPIO 4      3.3V   ├──── Hall Sensor VCC
         GND ───────┤ GND                │
                    │                     │
                    │        USB         │
                    └─────────┬───────────┘
                              │
                         Power/Program

Hall Sensor Wiring (A3144):
┌──────────────┐
│  A3144       │
│              │
│  VCC ────────┼──── ESP32 3.3V
│  GND ────────┼──── ESP32 GND
│  OUT ────────┼──── ESP32 GPIO 4 (with internal pull-up enabled)
└──────────────┘

Magnet Placement on Flywheel:
        ┌─────────────┐
       ╱               ╲
      │    ┌─────┐      │
      │    │ HUB │      │
      │    └─────┘      │
      │  M           M  │   M = Magnet (4-8 evenly spaced)
      │                 │
      │  M           M  │
       ╲               ╱
        └─────────────┘
```

---

## 3. Software Components

### 3.1 Component Overview

| Component | Language/Framework | Purpose |
|-----------|-------------------|---------|
| ESP32 Firmware | C++ / Arduino Framework | Core rowing monitor, HR receiver, web server |
| Galaxy Watch App | Tizen (pre-existing) | HeartRateToWeb app from Galaxy Store |
| Android App | Kotlin / Jetpack Compose | Session management and Health Connect sync |

### 3.2 Dependencies

#### ESP32 Firmware Dependencies

```ini
; PlatformIO libraries (platformio.ini)
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    bblanchon/ArduinoJson@^6.21.0
monitor_speed = 115200
board_build.filesystem = littlefs
```

#### Android App Dependencies

```kotlin
// build.gradle.kts (app level)
dependencies {
    // Health Connect
    implementation("androidx.health.connect:connect-client:1.1.0-alpha07")
    
    // Networking
    implementation("com.squareup.retrofit2:retrofit:2.9.0")
    implementation("com.squareup.retrofit2:converter-gson:2.9.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    
    // Compose UI
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.compose.material3:material3:1.2.0")
    implementation("androidx.compose.material:material-icons-extended:1.6.0")
    
    // Lifecycle
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
    
    // Coroutines
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}
```

---

## 4. ESP32 Firmware Implementation

### 4.1 Configuration Constants

```cpp
// ============== USER CONFIGURATION ==============
// WiFi Settings
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// mDNS hostname (access via http://rowing.local)
const char* MDNS_NAME = "rowing";

// Hardware pins
const int ROWING_SENSOR_PIN = 4;        // GPIO for hall sensor

// Session storage limits
const int MAX_SESSIONS = 10;            // Maximum stored sessions
const int MAX_HR_SAMPLES = 7200;        // Max HR samples per session (2 hours at 1Hz)

// Rowing machine calibration
const int MAGNETS_PER_REVOLUTION = 4;   // Number of magnets on flywheel
const float FLYWHEEL_INERTIA = 0.1001;  // kg*m² - adjust for your machine
const float DRAG_FACTOR_DEFAULT = 110;  // Default drag factor

// Timing
const unsigned long HR_STALE_TIMEOUT = 5000;      // HR considered stale after 5s
const unsigned long SENSOR_DEBOUNCE_US = 5000;    // 5ms debounce for sensor
const unsigned long CALC_INTERVAL_MS = 100;       // Calculate metrics every 100ms
```

### 4.2 Data Structures

```cpp
// Heart rate sample structure (compact for storage)
struct HRSample {
    unsigned long timestamp;  // Unix timestamp in milliseconds
    uint8_t bpm;              // Heart rate 0-255 (valid range 30-220)
};

// Rowing data sample structure
struct RowingSample {
    unsigned long timestamp;
    uint16_t power;           // Watts (0-2000 typical range)
    uint8_t strokeRate;       // Strokes per minute (0-60 typical)
    uint16_t pace;            // Seconds per 500m (0-600 typical)
};

// Complete session structure
struct Session {
    char id[20];              // Unique ID (timestamp string)
    unsigned long startTime;  // Session start (Unix ms)
    unsigned long endTime;    // Session end (Unix ms)
    float distance;           // Total meters rowed
    uint16_t strokes;         // Total stroke count
    uint16_t calories;        // Estimated calories burned
    float avgPower;           // Average power in watts
    uint8_t avgStrokeRate;    // Average strokes per minute
    uint8_t avgHeartRate;     // Average HR during session
    uint8_t maxHeartRate;     // Maximum HR during session
    bool synced;              // Has been synced to Health Connect
    uint16_t hrSampleCount;   // Number of HR samples stored
};

// File storage format:
// /sessions/index.json        - Array of session IDs
// /sessions/{id}.json         - Session metadata
// /sessions/{id}_hr.bin       - Binary HR samples (8 bytes each)
```

### 4.3 Complete ESP32 Firmware Source Code

```cpp
// ============================================================================
// ESP32 Rowing Monitor - Complete Firmware
// Version: 1.0.0
// 
// Features:
// - Rowing physics calculations (distance, power, pace, stroke rate)
// - Heart rate receiver (HeartRateToWeb compatible)
// - Multi-session storage with LittleFS
// - RESTful API for Android app integration
// - mDNS for easy discovery (http://rowing.local)
// ============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <ESPmDNS.h>
#include <math.h>

// ============== CONFIGURATION ==============
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MDNS_NAME = "rowing";

const int ROWING_SENSOR_PIN = 4;
const int MAX_SESSIONS = 10;
const int MAX_HR_SAMPLES = 7200;
const int MAGNETS_PER_REVOLUTION = 4;
const float FLYWHEEL_INERTIA = 0.1001;
const float DRAG_FACTOR_DEFAULT = 110.0;

const unsigned long HR_STALE_TIMEOUT = 5000;
const unsigned long SENSOR_DEBOUNCE_US = 5000;
const unsigned long CALC_INTERVAL_MS = 100;
const unsigned long IDLE_TIMEOUT_MS = 300000;  // 5 minutes

// ============== DATA STRUCTURES ==============
struct HRSample {
    unsigned long timestamp;
    uint8_t bpm;
};

struct Session {
    char id[20];
    unsigned long startTime;
    unsigned long endTime;
    float distance;
    uint16_t strokes;
    uint16_t calories;
    float avgPower;
    uint8_t avgStrokeRate;
    uint8_t avgHeartRate;
    uint8_t maxHeartRate;
    bool synced;
    uint16_t hrSampleCount;
};

// ============== GLOBAL STATE ==============
WebServer server(80);

// Heart rate state
volatile int currentHeartRate = 0;
volatile unsigned long lastHRUpdate = 0;

// Rowing sensor state (volatile for ISR)
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile unsigned long pulseCount = 0;
volatile bool newPulse = false;

// Rowing calculations
float dragFactor = DRAG_FACTOR_DEFAULT;
float totalDistance = 0;
int totalStrokes = 0;
float instantPower = 0;
float instantPace = 0;  // seconds per 500m
int strokeRate = 0;
float totalPowerSum = 0;
int powerSampleCount = 0;

// Stroke detection state
enum StrokePhase { DRIVE, RECOVERY };
StrokePhase currentPhase = RECOVERY;
unsigned long lastStrokeTime = 0;
float lastAngularVelocity = 0;

// Session state
Session currentSession;
bool workoutInProgress = false;
int sessionCount = 0;
String sessionIds[MAX_SESSIONS];

// HR buffer for current workout
HRSample hrBuffer[MAX_HR_SAMPLES];
int hrBufferIndex = 0;

// ============== INTERRUPT SERVICE ROUTINE ==============
void IRAM_ATTR onSensorPulse() {
    unsigned long now = micros();
    unsigned long interval = now - lastPulseTime;
    
    // Debounce: ignore pulses too close together
    if (interval > SENSOR_DEBOUNCE_US) {
        pulseInterval = interval;
        lastPulseTime = now;
        pulseCount++;
        newPulse = true;
    }
}

// ============== TIME FUNCTIONS ==============
void syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("Syncing NTP time");
    int attempts = 0;
    while (time(nullptr) < 1000000000 && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (time(nullptr) > 1000000000) {
        Serial.println(" OK!");
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        Serial.println(&timeinfo, "Current time: %Y-%m-%d %H:%M:%S");
    } else {
        Serial.println(" FAILED (will use millis)");
    }
}

unsigned long getTimestampMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL);
}

String generateSessionId() {
    return String(getTimestampMs());
}

// ============== STORAGE FUNCTIONS ==============
void initStorage() {
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
        return;
    }
    Serial.println("LittleFS mounted successfully");
    
    // Create sessions directory if needed
    if (!LittleFS.exists("/sessions")) {
        LittleFS.mkdir("/sessions");
    }
    
    loadSessionIndex();
    
    // Report storage info
    Serial.printf("Storage: %lu bytes used, %lu bytes total\n", 
                  LittleFS.usedBytes(), LittleFS.totalBytes());
}

void loadSessionIndex() {
    sessionCount = 0;
    File indexFile = LittleFS.open("/sessions/index.json", "r");
    if (!indexFile) {
        Serial.println("No session index found - starting fresh");
        return;
    }
    
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, indexFile);
    indexFile.close();
    
    if (error) {
        Serial.printf("Index parse error: %s\n", error.c_str());
        return;
    }
    
    JsonArray ids = doc["sessions"];
    for (JsonVariant id : ids) {
        if (sessionCount < MAX_SESSIONS) {
            sessionIds[sessionCount++] = id.as<String>();
        }
    }
    Serial.printf("Loaded %d sessions from index\n", sessionCount);
}

void saveSessionIndex() {
    File indexFile = LittleFS.open("/sessions/index.json", "w");
    if (!indexFile) {
        Serial.println("ERROR: Could not write session index");
        return;
    }
    
    StaticJsonDocument<1024> doc;
    JsonArray ids = doc.createNestedArray("sessions");
    for (int i = 0; i < sessionCount; i++) {
        ids.add(sessionIds[i]);
    }
    
    serializeJson(doc, indexFile);
    indexFile.close();
}

void saveSession(Session& session) {
    String path = "/sessions/" + String(session.id) + ".json";
    File file = LittleFS.open(path, "w");
    if (!file) {
        Serial.println("ERROR: Could not write session file");
        return;
    }
    
    StaticJsonDocument<512> doc;
    doc["id"] = session.id;
    doc["startTime"] = session.startTime;
    doc["endTime"] = session.endTime;
    doc["distance"] = session.distance;
    doc["strokes"] = session.strokes;
    doc["calories"] = session.calories;
    doc["avgPower"] = session.avgPower;
    doc["avgStrokeRate"] = session.avgStrokeRate;
    doc["avgHeartRate"] = session.avgHeartRate;
    doc["maxHeartRate"] = session.maxHeartRate;
    doc["synced"] = session.synced;
    doc["hrSampleCount"] = session.hrSampleCount;
    
    serializeJson(doc, file);
    file.close();
    
    Serial.printf("Session %s saved\n", session.id);
}

void saveHRSamples(const char* sessionId, HRSample* samples, int count) {
    String path = "/sessions/" + String(sessionId) + "_hr.bin";
    File file = LittleFS.open(path, "w");
    if (!file) {
        Serial.println("ERROR: Could not write HR samples");
        return;
    }
    
    for (int i = 0; i < count; i++) {
        file.write((uint8_t*)&samples[i], sizeof(HRSample));
    }
    file.close();
    
    Serial.printf("Saved %d HR samples for session %s\n", count, sessionId);
}

bool loadSession(const char* sessionId, Session& session) {
    String path = "/sessions/" + String(sessionId) + ".json";
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        return false;
    }
    
    strlcpy(session.id, doc["id"] | "", sizeof(session.id));
    session.startTime = doc["startTime"];
    session.endTime = doc["endTime"];
    session.distance = doc["distance"];
    session.strokes = doc["strokes"];
    session.calories = doc["calories"];
    session.avgPower = doc["avgPower"];
    session.avgStrokeRate = doc["avgStrokeRate"];
    session.avgHeartRate = doc["avgHeartRate"];
    session.maxHeartRate = doc["maxHeartRate"];
    session.synced = doc["synced"];
    session.hrSampleCount = doc["hrSampleCount"];
    
    return true;
}

void deleteOldestSession() {
    if (sessionCount == 0) return;
    
    String oldestId = sessionIds[0];
    Serial.printf("Deleting oldest session: %s\n", oldestId.c_str());
    
    LittleFS.remove("/sessions/" + oldestId + ".json");
    LittleFS.remove("/sessions/" + oldestId + "_hr.bin");
    
    // Shift array left
    for (int i = 0; i < sessionCount - 1; i++) {
        sessionIds[i] = sessionIds[i + 1];
    }
    sessionCount--;
    saveSessionIndex();
}

void markSessionSynced(const char* sessionId) {
    Session session;
    if (loadSession(sessionId, session)) {
        session.synced = true;
        saveSession(session);
        Serial.printf("Session %s marked as synced\n", sessionId);
    }
}

// ============== ROWING PHYSICS ==============
void updateRowingCalculations() {
    static unsigned long lastCalcTime = 0;
    unsigned long now = millis();
    
    if (now - lastCalcTime < CALC_INTERVAL_MS) return;
    lastCalcTime = now;
    
    if (!workoutInProgress) return;
    
    // Check for idle (no pulses for a while)
    if (now - (lastPulseTime / 1000) > 3000) {
        // No recent pulses - user might have stopped
        instantPower = 0;
        instantPace = 0;
        strokeRate = 0;
        return;
    }
    
    if (pulseInterval == 0) return;
    
    // Calculate angular velocity (rad/s)
    // pulseInterval is in microseconds, convert to seconds
    float revolutionsPerSecond = 1000000.0 / (pulseInterval * MAGNETS_PER_REVOLUTION);
    float angularVelocity = revolutionsPerSecond * 2.0 * PI;
    
    // Detect stroke phase transitions
    if (angularVelocity > lastAngularVelocity * 1.1 && currentPhase == RECOVERY) {
        // Acceleration detected - starting drive phase
        currentPhase = DRIVE;
    } else if (angularVelocity < lastAngularVelocity * 0.95 && currentPhase == DRIVE) {
        // Deceleration detected - starting recovery phase
        currentPhase = RECOVERY;
        
        // Count stroke
        unsigned long strokeInterval = now - lastStrokeTime;
        if (strokeInterval > 1000 && strokeInterval < 5000) {  // Valid stroke timing
            totalStrokes++;
            strokeRate = 60000 / strokeInterval;  // Convert to strokes per minute
            lastStrokeTime = now;
        }
    }
    
    lastAngularVelocity = angularVelocity;
    
    // Calculate instantaneous power using drag factor
    // Power = dragFactor * angularVelocity³
    instantPower = (dragFactor / 1000000.0) * pow(angularVelocity, 3);
    
    // Limit to reasonable range
    if (instantPower > 1000) instantPower = 1000;
    if (instantPower < 0) instantPower = 0;
    
    // Calculate pace (seconds per 500m)
    // Based on Concept2 formula: pace = (2.8 / power)^(1/3) * 500
    if (instantPower > 10) {
        instantPace = pow(2.8 / instantPower, 1.0/3.0) * 500.0;
        if (instantPace > 600) instantPace = 600;  // Cap at 10:00 per 500m
    } else {
        instantPace = 0;
    }
    
    // Accumulate distance
    // Using work = power * time, then converting to distance
    float timeInterval = CALC_INTERVAL_MS / 1000.0;
    float workDone = instantPower * timeInterval;  // Joules
    // Approximate distance from work (simplified)
    float distanceIncrement = pow(workDone / 2.8, 1.0/3.0) * timeInterval;
    if (distanceIncrement > 0 && distanceIncrement < 10) {
        totalDistance += distanceIncrement;
    }
    
    // Accumulate for averages
    if (instantPower > 0) {
        totalPowerSum += instantPower;
        powerSampleCount++;
    }
}

// ============== HEART RATE HANDLERS ==============
void handleHRPost() {
    int hr = 0;
    
    if (server.hasArg("plain")) {
        // Raw body: "75"
        String body = server.arg("plain");
        body.trim();
        hr = body.toInt();
    } else if (server.hasArg("bpm")) {
        // Query parameter: ?bpm=75
        hr = server.arg("bpm").toInt();
    } else if (server.hasArg("hr")) {
        // Alternative: ?hr=75
        hr = server.arg("hr").toInt();
    }
    
    if (hr > 0 && hr < 255) {
        currentHeartRate = hr;
        lastHRUpdate = millis();
        
        // Store sample if workout in progress
        if (workoutInProgress && hrBufferIndex < MAX_HR_SAMPLES) {
            hrBuffer[hrBufferIndex].timestamp = getTimestampMs();
            hrBuffer[hrBufferIndex].bpm = (uint8_t)hr;
            hrBufferIndex++;
        }
        
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Invalid HR value");
    }
}

void handleHRGet() {
    // Return 0 if data is stale
    if (millis() - lastHRUpdate > HR_STALE_TIMEOUT) {
        server.send(200, "text/plain", "0");
    } else {
        server.send(200, "text/plain", String(currentHeartRate));
    }
}

// ============== WORKOUT CONTROL HANDLERS ==============
void handleStartWorkout() {
    if (workoutInProgress) {
        server.send(400, "application/json", "{\"error\":\"Workout already in progress\"}");
        return;
    }
    
    // Initialize new session
    memset(&currentSession, 0, sizeof(Session));
    String id = generateSessionId();
    strlcpy(currentSession.id, id.c_str(), sizeof(currentSession.id));
    currentSession.startTime = getTimestampMs();
    currentSession.synced = false;
    
    // Reset workout state
    hrBufferIndex = 0;
    totalDistance = 0;
    totalStrokes = 0;
    totalPowerSum = 0;
    powerSampleCount = 0;
    instantPower = 0;
    instantPace = 0;
    strokeRate = 0;
    pulseCount = 0;
    currentPhase = RECOVERY;
    lastStrokeTime = millis();
    
    workoutInProgress = true;
    
    Serial.printf("Workout started: %s\n", currentSession.id);
    
    StaticJsonDocument<128> doc;
    doc["status"] = "started";
    doc["sessionId"] = currentSession.id;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleStopWorkout() {
    if (!workoutInProgress) {
        server.send(400, "application/json", "{\"error\":\"No workout in progress\"}");
        return;
    }
    
    // Finalize session data
    currentSession.endTime = getTimestampMs();
    currentSession.distance = totalDistance;
    currentSession.strokes = totalStrokes;
    currentSession.hrSampleCount = hrBufferIndex;
    
    // Calculate average power
    if (powerSampleCount > 0) {
        currentSession.avgPower = totalPowerSum / powerSampleCount;
    }
    
    // Calculate average stroke rate
    unsigned long durationMs = currentSession.endTime - currentSession.startTime;
    if (durationMs > 0 && totalStrokes > 0) {
        currentSession.avgStrokeRate = (totalStrokes * 60000) / durationMs;
    }
    
    // Calculate HR statistics
    if (hrBufferIndex > 0) {
        long sumHR = 0;
        int maxHR = 0;
        for (int i = 0; i < hrBufferIndex; i++) {
            sumHR += hrBuffer[i].bpm;
            if (hrBuffer[i].bpm > maxHR) {
                maxHR = hrBuffer[i].bpm;
            }
        }
        currentSession.avgHeartRate = sumHR / hrBufferIndex;
        currentSession.maxHeartRate = maxHR;
    }
    
    // Estimate calories (MET-based formula)
    // Rowing at moderate effort ≈ 7 METs
    // Calories = MET * weight(kg) * duration(hours)
    // Assuming 70kg user, or use avg HR if available
    float durationHours = durationMs / 3600000.0;
    float mets = 7.0;
    if (currentSession.avgHeartRate > 0) {
        // Adjust METs based on HR (simplified)
        mets = 5.0 + (currentSession.avgHeartRate - 100) * 0.05;
        if (mets < 4) mets = 4;
        if (mets > 12) mets = 12;
    }
    currentSession.calories = (int)(mets * 70.0 * durationHours);
    
    // Make room for new session if needed
    if (sessionCount >= MAX_SESSIONS) {
        deleteOldestSession();
    }
    
    // Save session and HR samples
    saveSession(currentSession);
    saveHRSamples(currentSession.id, hrBuffer, hrBufferIndex);
    
    // Add to session index
    sessionIds[sessionCount++] = String(currentSession.id);
    saveSessionIndex();
    
    workoutInProgress = false;
    
    Serial.printf("Workout stopped: %s (%.0fm, %d strokes, %d HR samples)\n",
                  currentSession.id, currentSession.distance, 
                  currentSession.strokes, currentSession.hrSampleCount);
    
    StaticJsonDocument<256> doc;
    doc["status"] = "stopped";
    doc["sessionId"] = currentSession.id;
    doc["distance"] = currentSession.distance;
    doc["strokes"] = currentSession.strokes;
    doc["calories"] = currentSession.calories;
    doc["hrSamples"] = currentSession.hrSampleCount;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// ============== STATUS AND SESSION HANDLERS ==============
void handleStatus() {
    StaticJsonDocument<512> doc;
    doc["online"] = true;
    doc["workoutInProgress"] = workoutInProgress;
    doc["sessionCount"] = sessionCount;
    doc["currentHeartRate"] = (millis() - lastHRUpdate < HR_STALE_TIMEOUT) ? currentHeartRate : 0;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["storageUsed"] = LittleFS.usedBytes();
    doc["storageTotal"] = LittleFS.totalBytes();
    
    if (workoutInProgress) {
        unsigned long duration = (getTimestampMs() - currentSession.startTime) / 1000;
        doc["currentSessionId"] = currentSession.id;
        doc["currentDistance"] = totalDistance;
        doc["currentStrokes"] = totalStrokes;
        doc["currentPower"] = instantPower;
        doc["currentPace"] = instantPace;
        doc["currentStrokeRate"] = strokeRate;
        doc["duration"] = duration;
        doc["hrSamples"] = hrBufferIndex;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleListSessions() {
    DynamicJsonDocument doc(8192);
    JsonArray sessions = doc.createNestedArray("sessions");
    
    for (int i = sessionCount - 1; i >= 0; i--) {  // Newest first
        Session session;
        if (loadSession(sessionIds[i].c_str(), session)) {
            JsonObject obj = sessions.createNestedObject();
            obj["id"] = session.id;
            obj["startTime"] = session.startTime;
            obj["endTime"] = session.endTime;
            obj["distance"] = session.distance;
            obj["strokes"] = session.strokes;
            obj["calories"] = session.calories;
            obj["avgPower"] = session.avgPower;
            obj["avgStrokeRate"] = session.avgStrokeRate;
            obj["avgHeartRate"] = session.avgHeartRate;
            obj["maxHeartRate"] = session.maxHeartRate;
            obj["duration"] = (session.endTime - session.startTime) / 1000;
            obj["synced"] = session.synced;
            obj["hrSampleCount"] = session.hrSampleCount;
        }
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleGetSession() {
    String uri = server.uri();
    String sessionId = uri.substring(10);  // Remove "/sessions/"
    
    // Remove trailing slash if present
    if (sessionId.endsWith("/")) {
        sessionId = sessionId.substring(0, sessionId.length() - 1);
    }
    
    Session session;
    if (!loadSession(sessionId.c_str(), session)) {
        server.send(404, "application/json", "{\"error\":\"Session not found\"}");
        return;
    }
    
    // Allocate large buffer for HR samples
    DynamicJsonDocument doc(65536);
    
    doc["id"] = session.id;
    doc["startTime"] = session.startTime;
    doc["endTime"] = session.endTime;
    doc["distance"] = session.distance;
    doc["strokes"] = session.strokes;
    doc["calories"] = session.calories;
    doc["avgPower"] = session.avgPower;
    doc["avgStrokeRate"] = session.avgStrokeRate;
    doc["avgHeartRate"] = session.avgHeartRate;
    doc["maxHeartRate"] = session.maxHeartRate;
    doc["duration"] = (session.endTime - session.startTime) / 1000;
    doc["synced"] = session.synced;
    
    // Load and include HR samples
    JsonArray hrArray = doc.createNestedArray("heartRateSamples");
    String hrPath = "/sessions/" + sessionId + "_hr.bin";
    File hrFile = LittleFS.open(hrPath, "r");
    if (hrFile) {
        HRSample sample;
        int count = 0;
        while (hrFile.read((uint8_t*)&sample, sizeof(HRSample)) == sizeof(HRSample)) {
            JsonObject obj = hrArray.createNestedObject();
            obj["time"] = sample.timestamp;
            obj["bpm"] = sample.bpm;
            count++;
            
            // Safety limit
            if (count >= MAX_HR_SAMPLES) break;
        }
        hrFile.close();
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleMarkSynced() {
    String uri = server.uri();
    // URI format: /sessions/{id}/synced
    int start = 10;  // After "/sessions/"
    int end = uri.lastIndexOf('/');
    String sessionId = uri.substring(start, end);
    
    markSessionSynced(sessionId.c_str());
    server.send(200, "application/json", "{\"status\":\"marked as synced\"}");
}

void handleDeleteSession() {
    String uri = server.uri();
    String sessionId = uri.substring(10);
    
    if (sessionId.endsWith("/")) {
        sessionId = sessionId.substring(0, sessionId.length() - 1);
    }
    
    // Remove files
    LittleFS.remove("/sessions/" + sessionId + ".json");
    LittleFS.remove("/sessions/" + sessionId + "_hr.bin");
    
    // Remove from index
    for (int i = 0; i < sessionCount; i++) {
        if (sessionIds[i] == sessionId) {
            for (int j = i; j < sessionCount - 1; j++) {
                sessionIds[j] = sessionIds[j + 1];
            }
            sessionCount--;
            break;
        }
    }
    saveSessionIndex();
    
    Serial.printf("Deleted session: %s\n", sessionId.c_str());
    server.send(200, "application/json", "{\"status\":\"deleted\"}");
}

void handleLiveData() {
    if (!workoutInProgress) {
        server.send(404, "application/json", "{\"error\":\"No workout in progress\"}");
        return;
    }
    
    StaticJsonDocument<512> doc;
    unsigned long duration = (getTimestampMs() - currentSession.startTime) / 1000;
    
    doc["sessionId"] = currentSession.id;
    doc["distance"] = totalDistance;
    doc["strokes"] = totalStrokes;
    doc["duration"] = duration;
    doc["power"] = instantPower;
    doc["pace"] = instantPace;
    doc["strokeRate"] = strokeRate;
    doc["heartRate"] = (millis() - lastHRUpdate < HR_STALE_TIMEOUT) ? currentHeartRate : 0;
    doc["hrSamples"] = hrBufferIndex;
    doc["phase"] = (currentPhase == DRIVE) ? "drive" : "recovery";
    
    // Calculate current averages
    if (duration > 0) {
        doc["avgPace"] = (totalDistance > 0) ? (duration * 500.0 / totalDistance) : 0;
    }
    if (powerSampleCount > 0) {
        doc["avgPower"] = totalPowerSum / powerSampleCount;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// ============== WEB SERVER SETUP ==============
void setupWebServer() {
    // Enable CORS for browser/app access
    server.enableCORS(true);
    
    // HeartRateToWeb compatible endpoints
    server.on("/hr", HTTP_POST, handleHRPost);
    server.on("/hr", HTTP_GET, handleHRGet);
    
    // Status endpoint
    server.on("/status", HTTP_GET, handleStatus);
    
    // Workout control
    server.on("/workout/start", HTTP_POST, handleStartWorkout);
    server.on("/workout/stop", HTTP_POST, handleStopWorkout);
    server.on("/live", HTTP_GET, handleLiveData);
    
    // Session list
    server.on("/sessions", HTTP_GET, handleListSessions);
    
    // Handle dynamic session routes
    server.onNotFound([]() {
        String uri = server.uri();
        HTTPMethod method = server.method();
        
        if (uri.startsWith("/sessions/")) {
            if (uri.endsWith("/synced") && method == HTTP_POST) {
                handleMarkSynced();
            } else if (method == HTTP_GET) {
                handleGetSession();
            } else if (method == HTTP_DELETE) {
                handleDeleteSession();
            } else {
                server.send(405, "text/plain", "Method Not Allowed");
            }
        } else {
            server.send(404, "text/plain", "Not Found");
        }
    });
    
    server.begin();
    Serial.println("HTTP server started on port 80");
}

// ============== MAIN SETUP ==============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n");
    Serial.println("╔════════════════════════════════════════════╗");
    Serial.println("║    ESP32 Rowing Monitor v1.0.0             ║");
    Serial.println("╚════════════════════════════════════════════╝");
    Serial.println();
    
    // Initialize storage
    Serial.println("[1/5] Initializing storage...");
    initStorage();
    
    // Setup rowing sensor
    Serial.println("[2/5] Configuring rowing sensor...");
    pinMode(ROWING_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ROWING_SENSOR_PIN), onSensorPulse, FALLING);
    Serial.printf("       Hall sensor on GPIO %d\n", ROWING_SENSOR_PIN);
    
    // Connect to WiFi
    Serial.println("[3/5] Connecting to WiFi...");
    Serial.printf("       SSID: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
        delay(500);
        Serial.print(".");
        wifiAttempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" Connected!");
        Serial.printf("       IP Address: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(" FAILED!");
        Serial.println("       Restarting in 5 seconds...");
        delay(5000);
        ESP.restart();
    }
    
    // Setup mDNS
    Serial.println("[4/5] Starting mDNS...");
    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("       mDNS: http://%s.local\n", MDNS_NAME);
    } else {
        Serial.println("       mDNS FAILED - use IP address instead");
    }
    
    // Sync time
    Serial.println("[5/5] Synchronizing time...");
    syncTime();
    
    // Start web server
    setupWebServer();
    
    Serial.println();
    Serial.println("╔════════════════════════════════════════════╗");
    Serial.println("║              SYSTEM READY                  ║");
    Serial.println("╚════════════════════════════════════════════╝");
    Serial.printf("Access at: http://%s.local or http://%s\n", 
                  MDNS_NAME, WiFi.localIP().toString().c_str());
    Serial.printf("Sessions stored: %d/%d\n", sessionCount, MAX_SESSIONS);
    Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.println();
}

// ============== MAIN LOOP ==============
void loop() {
    // Handle HTTP requests
    server.handleClient();
    
    // Update rowing calculations
    updateRowingCalculations();
    
    // Handle mDNS
    MDNS.update();
    
    // Small delay to prevent watchdog issues
    delay(1);
}
```

### 4.4 PlatformIO Configuration

```ini
; platformio.ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs
lib_deps = 
    bblanchon/ArduinoJson@^6.21.0
build_flags = 
    -DCORE_DEBUG_LEVEL=0
    -DCONFIG_ARDUHAL_LOG_COLORS=1
upload_speed = 921600
```

---

## 5. Galaxy Watch Configuration

### 5.1 HeartRateToWeb App Setup

The Galaxy Watch uses the existing HeartRateToWeb Tizen app. No custom development required.

#### Installation Steps

1. Open Galaxy Store on your Samsung Galaxy Watch
2. Search for "HeartRateToWeb" or "Heart Rate to Web"
3. Install the app
4. Note: If not available, sideload from GitHub releases: https://github.com/loic2665/HeartRateToWeb

#### Configuration Steps

1. Ensure Galaxy Watch is connected to same WiFi as ESP32
2. Open HeartRateToWeb app on watch
3. Enter ESP32 address: `rowing.local` or IP address (e.g., `192.168.1.50`)
4. Enter port: `80`
5. Tap "Start" to begin sending heart rate data
6. The app will POST to `http://rowing.local/hr` every second

### 5.2 Compatible Watches

| Watch Model | Compatibility | Notes |
|-------------|--------------|-------|
| Galaxy Watch 7/6/5/4 | ✅ Full | Wear OS based |
| Galaxy Watch 3/Active 2/Active | ✅ Full | Tizen based |
| Galaxy Watch (Original) | ✅ Full | Tizen based |
| Gear S3/Sport | ✅ Full | Tizen based |
| Gear S2 | ✅ Likely | Tizen 2.3.2+ required |
| Galaxy Fit | ❌ No | No app store |

### 5.3 Troubleshooting Watch Connection

| Issue | Solution |
|-------|----------|
| Watch can't find ESP32 | Ensure both on same WiFi network (2.4GHz) |
| Connection refused | Verify ESP32 is running and IP is correct |
| HR not updating on ESP32 | Check watch has granted heart rate permission |
| Intermittent connection | Move closer to WiFi router |

---

## 6. Android App Implementation

### 6.1 Project Setup

#### Android Studio Project Configuration

```
Project Name: RowingSync
Package Name: com.yourname.rowingsync
Minimum SDK: API 28 (Android 9.0)
Target SDK: API 34 (Android 14)
Language: Kotlin
Build System: Gradle Kotlin DSL
```

### 6.2 Manifest Configuration

```xml
<!-- AndroidManifest.xml -->
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:tools="http://schemas.android.com/tools">

    <!-- Network permissions -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />
    
    <!-- Health Connect permissions -->
    <uses-permission android:name="android.permission.health.READ_HEART_RATE" />
    <uses-permission android:name="android.permission.health.WRITE_HEART_RATE" />
    <uses-permission android:name="android.permission.health.READ_EXERCISE" />
    <uses-permission android:name="android.permission.health.WRITE_EXERCISE" />
    <uses-permission android:name="android.permission.health.READ_DISTANCE" />
    <uses-permission android:name="android.permission.health.WRITE_DISTANCE" />
    <uses-permission android:name="android.permission.health.READ_TOTAL_CALORIES_BURNED" />
    <uses-permission android:name="android.permission.health.WRITE_TOTAL_CALORIES_BURNED" />
    
    <application
        android:name=".RowingSyncApplication"
        android:allowBackup="true"
        android:icon="@mipmap/ic_launcher"
        android:label="@string/app_name"
        android:roundIcon="@mipmap/ic_launcher_round"
        android:supportsRtl="true"
        android:theme="@style/Theme.RowingSync"
        android:networkSecurityConfig="@xml/network_security_config"
        tools:targetApi="34">
        
        <activity
            android:name=".MainActivity"
            android:exported="true"
            android:theme="@style/Theme.RowingSync">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
            
            <!-- Health Connect permissions intent filter -->
            <intent-filter>
                <action android:name="androidx.health.ACTION_SHOW_PERMISSIONS_RATIONALE" />
            </intent-filter>
        </activity>
        
        <!-- Health Connect permissions activity -->
        <activity
            android:name="androidx.health.connect.client.permission.HealthDataRequestPermissionsActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="androidx.health.ACTION_REQUEST_PERMISSIONS" />
            </intent-filter>
        </activity>
        
        <!-- Declare Health Connect data types used -->
        <meta-data
            android:name="health_permissions"
            android:resource="@array/health_permissions" />
    </application>
</manifest>
```

### 6.3 Network Security Configuration

```xml
<!-- res/xml/network_security_config.xml -->
<?xml version="1.0" encoding="utf-8"?>
<network-security-config>
    <!-- Allow cleartext HTTP for local network ESP32 access -->
    <domain-config cleartextTrafficPermitted="true">
        <domain includeSubdomains="true">rowing.local</domain>
        <domain includeSubdomains="true">192.168.0.0/16</domain>
        <domain includeSubdomains="true">10.0.0.0/8</domain>
        <domain includeSubdomains="true">172.16.0.0/12</domain>
    </domain-config>
</network-security-config>
```

### 6.4 Health Permissions Resource

```xml
<!-- res/values/health_permissions.xml -->
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <array name="health_permissions">
        <item>androidx.health.permission.ExerciseSession.READ</item>
        <item>androidx.health.permission.ExerciseSession.WRITE</item>
        <item>androidx.health.permission.HeartRate.READ</item>
        <item>androidx.health.permission.HeartRate.WRITE</item>
        <item>androidx.health.permission.Distance.READ</item>
        <item>androidx.health.permission.Distance.WRITE</item>
        <item>androidx.health.permission.TotalCaloriesBurned.READ</item>
        <item>androidx.health.permission.TotalCaloriesBurned.WRITE</item>
    </array>
</resources>
```

### 6.5 Gradle Build Configuration

```kotlin
// build.gradle.kts (Project level)
plugins {
    id("com.android.application") version "8.2.0" apply false
    id("org.jetbrains.kotlin.android") version "1.9.21" apply false
}
```

```kotlin
// build.gradle.kts (App level)
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.yourname.rowingsync"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.yourname.rowingsync"
        minSdk = 28
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    
    kotlinOptions {
        jvmTarget = "17"
    }
    
    buildFeatures {
        compose = true
    }
    
    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.7"
    }
}

dependencies {
    // Core Android
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.activity:activity-compose:1.8.2")
    
    // Compose
    implementation(platform("androidx.compose:compose-bom:2024.01.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    
    // Health Connect
    implementation("androidx.health.connect:connect-client:1.1.0-alpha07")
    
    // Networking
    implementation("com.squareup.retrofit2:retrofit:2.9.0")
    implementation("com.squareup.retrofit2:converter-gson:2.9.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("com.squareup.okhttp3:logging-interceptor:4.12.0")
    
    // Coroutines
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
    
    // DataStore for preferences
    implementation("androidx.datastore:datastore-preferences:1.0.0")
    
    // Debug
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
```

### 6.6 Data Models

```kotlin
// data/Models.kt
package com.yourname.rowingsync.data

data class StatusResponse(
    val online: Boolean,
    val workoutInProgress: Boolean,
    val sessionCount: Int,
    val currentHeartRate: Int,
    val freeHeap: Long,
    val uptime: Long,
    val storageUsed: Long,
    val storageTotal: Long,
    // Optional fields when workout in progress
    val currentSessionId: String? = null,
    val currentDistance: Float? = null,
    val currentStrokes: Int? = null,
    val currentPower: Float? = null,
    val currentPace: Float? = null,
    val currentStrokeRate: Int? = null,
    val duration: Long? = null,
    val hrSamples: Int? = null
)

data class SessionSummary(
    val id: String,
    val startTime: Long,
    val endTime: Long,
    val distance: Float,
    val strokes: Int,
    val calories: Int,
    val avgPower: Float,
    val avgStrokeRate: Int,
    val avgHeartRate: Int,
    val maxHeartRate: Int,
    val duration: Int,
    val synced: Boolean,
    val hrSampleCount: Int
)

data class SessionsResponse(
    val sessions: List<SessionSummary>
)

data class HRSample(
    val time: Long,
    val bpm: Int
)

data class SessionDetail(
    val id: String,
    val startTime: Long,
    val endTime: Long,
    val distance: Float,
    val strokes: Int,
    val calories: Int,
    val avgPower: Float,
    val avgStrokeRate: Int,
    val avgHeartRate: Int,
    val maxHeartRate: Int,
    val duration: Int,
    val synced: Boolean,
    val heartRateSamples: List<HRSample>
)

data class LiveData(
    val sessionId: String,
    val distance: Float,
    val strokes: Int,
    val duration: Long,
    val power: Float,
    val pace: Float,
    val strokeRate: Int,
    val heartRate: Int,
    val hrSamples: Int,
    val phase: String,
    val avgPace: Float? = null,
    val avgPower: Float? = null
)

data class WorkoutResponse(
    val status: String,
    val sessionId: String? = null,
    val distance: Float? = null,
    val strokes: Int? = null,
    val calories: Int? = null,
    val hrSamples: Int? = null
)

data class GenericResponse(
    val status: String? = null,
    val error: String? = null
)
```

### 6.7 API Service

```kotlin
// data/Esp32Api.kt
package com.yourname.rowingsync.data

import retrofit2.http.*

interface Esp32Api {
    @GET("status")
    suspend fun getStatus(): StatusResponse
    
    @GET("sessions")
    suspend fun getSessions(): SessionsResponse
    
    @GET("sessions/{id}")
    suspend fun getSession(@Path("id") id: String): SessionDetail
    
    @POST("sessions/{id}/synced")
    suspend fun markSynced(@Path("id") id: String): GenericResponse
    
    @DELETE("sessions/{id}")
    suspend fun deleteSession(@Path("id") id: String): GenericResponse
    
    @POST("workout/start")
    suspend fun startWorkout(): WorkoutResponse
    
    @POST("workout/stop")
    suspend fun stopWorkout(): WorkoutResponse
    
    @GET("live")
    suspend fun getLiveData(): LiveData
}
```

### 6.8 API Client

```kotlin
// data/ApiClient.kt
package com.yourname.rowingsync.data

import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.util.concurrent.TimeUnit

object ApiClient {
    private var currentBaseUrl: String = "http://rowing.local/"
    private var retrofit: Retrofit? = null
    private var api: Esp32Api? = null
    
    private val okHttpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .writeTimeout(10, TimeUnit.SECONDS)
        .addInterceptor(HttpLoggingInterceptor().apply {
            level = HttpLoggingInterceptor.Level.BODY
        })
        .build()
    
    fun getApi(baseUrl: String): Esp32Api {
        val normalizedUrl = if (baseUrl.endsWith("/")) baseUrl else "$baseUrl/"
        val fullUrl = if (normalizedUrl.startsWith("http")) normalizedUrl else "http://$normalizedUrl"
        
        if (api == null || currentBaseUrl != fullUrl) {
            currentBaseUrl = fullUrl
            retrofit = Retrofit.Builder()
                .baseUrl(fullUrl)
                .client(okHttpClient)
                .addConverterFactory(GsonConverterFactory.create())
                
