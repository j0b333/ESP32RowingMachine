# ESP32 Rowing Monitor - API Reference

This document describes all HTTP REST APIs and WebSocket interfaces provided by the ESP32 Rowing Monitor.

## Base URL

- **Access Point Mode:** `http://192.168.4.1`
- **Station Mode:** `http://rowing.local` (mDNS) or the assigned IP address

## Authentication

No authentication is required. The device operates on a private WiFi network.

---

## REST API Endpoints

### Status & Metrics

#### GET /api/status

Returns device status and current session information.

**Response:**
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

| Field | Type | Description |
|-------|------|-------------|
| `version` | string | Firmware version |
| `device` | string | Device name |
| `online` | boolean | Device is operational |
| `workoutInProgress` | boolean | A workout session is active |
| `sessionCount` | number | Number of stored sessions |
| `currentHeartRate` | number | Current heart rate (0 if not available) |
| `freeHeap` | number | Free heap memory in bytes |
| `uptime` | number | Device uptime in seconds |
| `bleConnected` | boolean | BLE HR monitor connected |
| `wsClients` | number | Active WebSocket clients |

---

#### GET /api/metrics

Returns current rowing metrics.

**Response:**
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
    "phase": "drive",
    "heart_rate": 145
}
```

| Field | Type | Description |
|-------|------|-------------|
| `active` | boolean | Currently rowing |
| `elapsed_ms` | number | Session time in milliseconds |
| `time` | string | Formatted time (MM:SS.s) |
| `distance` | number | Total distance in meters |
| `pace` | number | Pace in seconds per 500m |
| `pace_str` | string | Formatted pace (M:SS.s) |
| `power` | number | Power output in watts |
| `spm` | number | Strokes per minute |
| `strokes` | number | Total stroke count |
| `calories` | number | Calories burned |
| `drag` | number | Drag factor |
| `phase` | string | Current stroke phase: `idle`, `drive`, or `recovery` |
| `heart_rate` | number | Current heart rate (0 if unavailable) |

---

### Configuration

#### GET /api/config

Returns current device configuration.

**Response:**
```json
{
    "moment_of_inertia": 0.101,
    "drag_coefficient": 0.00012,
    "user_weight_kg": 75.0,
    "wifi_ssid": "CrivitRower",
    "device_name": "Crivit Rower",
    "show_power": true,
    "show_calories": true,
    "units": "metric",
    "auto_pause_seconds": 5
}
```

---

#### POST /api/config

Updates device configuration.

**Request Body:**
```json
{
    "user_weight_kg": 80.0,
    "show_power": true,
    "units": "metric"
}
```

**Response:**
```json
{
    "status": "ok"
}
```

---

#### POST /api/reset

Resets current session metrics without ending the workout.

**Response:**
```json
{
    "status": "ok"
}
```

---

### Heart Rate

#### POST /hr

Receives heart rate from external apps (HeartRateToWeb compatible).

**Request Body (plain text):**
```
75
```

**Or URL parameter:**
```
POST /hr?bpm=75
```

**Response:**
- `200 OK` - "OK"
- `400 Bad Request` - "Invalid HR value"

---

#### GET /hr

Returns current heart rate.

**Response (plain text):**
```
72
```

Returns `0` if heart rate data is stale (>5 seconds old).

---

### Workout Control

#### POST /workout/start

Starts a new workout session.

**Response:**
```json
{
    "status": "ok",
    "session_id": 4
}
```

---

#### POST /workout/stop

Stops the current workout and saves the session.

**Response:**
```json
{
    "status": "ok",
    "session_id": 4,
    "duration": 1800,
    "distance": 6000.5
}
```

---

#### GET /live

Returns live workout data (alternative to WebSocket).

**Response:** Same as `/api/metrics`

---

### Session Management

#### GET /api/sessions

Lists all stored workout sessions.

**Response:**
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
            "sampleCount": 1800
        }
    ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | number | Unique session identifier |
| `startTime` | number | Unix timestamp (milliseconds) |
| `duration` | number | Duration in seconds |
| `distance` | number | Total distance in meters |
| `strokes` | number | Total strokes |
| `calories` | number | Calories burned |
| `avgPower` | number | Average power (watts) |
| `avgPace` | number | Average pace (sec/500m) |
| `avgHeartRate` | number | Average heart rate |
| `maxHeartRate` | number | Maximum heart rate |
| `synced` | boolean | Synced to companion app |
| `sampleCount` | number | Per-second data samples |

---

#### GET /api/sessions/{id}

Gets detailed session data including per-second samples.

**Response:**
```json
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
    "samples": [
        {
            "t": 0,
            "p": 120,
            "v": 350,
            "hr": 72,
            "d": 25
        },
        {
            "t": 1,
            "p": 185,
            "v": 420,
            "hr": 98,
            "d": 42
        }
    ]
}
```

**Sample fields:**

| Field | Type | Description |
|-------|------|-------------|
| `t` | number | Time offset in seconds |
| `p` | number | Power in watts |
| `v` | number | Velocity in cm/s |
| `hr` | number | Heart rate |
| `d` | number | Distance delta in decimeters |

---

#### POST /api/sessions/{id}/synced

Marks a session as synced to the companion app.

**Response:**
```json
{
    "status": "ok"
}
```

---

#### DELETE /api/sessions/{id}

Deletes a session.

**Response:**
```json
{
    "status": "ok"
}
```

---

### Calibration

#### POST /api/calibrate/inertia

Starts the flywheel moment of inertia calibration process.

**Response:**
```json
{
    "status": "started",
    "message": "Spin up the flywheel and let it coast to a stop"
}
```

---

#### GET /api/calibrate/inertia

Gets the current calibration status.

**Response:**
```json
{
    "state": "spindown",
    "message": "Tracking spindown...",
    "peak_velocity": 45.2,
    "calculated_inertia": 0.0,
    "sample_count": 156
}
```

**States:**
- `idle` - Not calibrating
- `waiting` - Waiting for user to spin flywheel
- `spinup` - Detecting peak velocity
- `spindown` - Tracking deceleration
- `complete` - Calibration finished
- `failed` - Calibration failed

---

## WebSocket Interface

### Connection

Connect to: `ws://192.168.4.1/ws` or `ws://rowing.local/ws`

### Messages (Server → Client)

The server broadcasts rowing metrics every 200ms:

```json
{
    "type": "metrics",
    "data": {
        "active": true,
        "elapsed_ms": 1234567,
        "distance": 4500.5,
        "pace": 120.5,
        "power": 185.3,
        "spm": 24.5,
        "strokes": 502,
        "calories": 350,
        "heart_rate": 145,
        "phase": "drive"
    }
}
```

### Session Events

When a workout starts:
```json
{
    "type": "session_start",
    "session_id": 4
}
```

When a workout ends:
```json
{
    "type": "session_end",
    "session_id": 4
}
```

### Heart Rate Updates

When heart rate is received:
```json
{
    "type": "heart_rate",
    "bpm": 145,
    "source": "ble"
}
```

---

## Error Handling

### HTTP Status Codes

| Code | Description |
|------|-------------|
| 200 | Success |
| 400 | Bad request (invalid parameters) |
| 404 | Resource not found |
| 500 | Internal server error |

### Error Response Format

```json
{
    "error": "Description of the error",
    "code": 400
}
```

---

## Rate Limits

- REST API: No explicit rate limits
- WebSocket: Server broadcasts at 5 Hz (200ms interval)
- Heart rate POST: Recommended max 1 Hz

---

## CORS

The server includes CORS headers allowing requests from any origin:
```
Access-Control-Allow-Origin: *
```
