# **COMPLETE ESP32 ROWING MONITOR PROJECT SPECIFICATION**
## **Single Document for LLM Agent Code Generation**

---

# **PROJECT OVERVIEW**

Create a complete ESP-IDF firmware project that transforms a Crivit branded rowing machine into a smart fitness device with:
- Bluetooth Low Energy FTMS (Fitness Machine Service) support for fitness app integration
- Real-time web interface accessible via smartphone/tablet browser
- Advanced physics-based rowing metrics calculations
- Google Health Connect compatibility via BLE
- Professional-grade sensor processing and stroke detection

**Target Hardware:** ESP32-DevKitC-V1 (ESP32-WROOM-32)
**Development Framework:** ESP-IDF v5.0 or later
**Programming Language:** C (with minimal C++ if needed)

---

# **HARDWARE CONFIGURATION**

## **Power Setup**

```
Power Architecture:
┌─────────────────────────────────────────────┐
│  USB Cable (5V, 500mA+)                     │
│      ↓                                      │
│  ESP32 DevKit-C                             │
│      ├─ VIN (5V input from USB)            │
│      ├─ GND (USB ground)                    │
│      └─ 3.3V output pin → Rowing machine   │
│         (Powers sensor pull-ups, max 200mA) │
└─────────────────────────────────────────────┘

Note: ESP32's 3.3V regulator can supply ~600mA total
Sensor circuit draws ~10-20mA, well within limits
```

## **Sensor Connections**

### **Confirmed Sensor Configuration**
Based on physical testing with multimeter:

```
Rowing Machine 4-Wire Connector:
┌──────────────────────────────────────────────────┐
│  White Wire:  Flywheel reed switch signal        │
│               - Active LOW (3.2V rest, 0V active)│
│               - Triggers once per flywheel rev   │
│               - Connect to: ESP32 GPIO 15        │
│                                                  │
│  Red Wire:    Seat position reed switch signal   │
│               - Active LOW (3.2V rest, 0V active)│
│               - Triggers at seat mid-rail        │
│               - Connect to: ESP32 GPIO 16        │
│                                                  │
│  Yellow Wire: Ground (shared)                    │
│               - Connect to: ESP32 GND            │
│                                                  │
│  Black Wire:  Ground (shared, redundant)         │
│               - Connect to: ESP32 GND            │
└──────────────────────────────────────────────────┘

CRITICAL: The rowing machine's sensor circuit requires 
3.3V power supply. Connect ESP32 3.3V pin to the machine's
Red wire (which originally received 3.2V from 2xAA batteries).
```

## **Complete Wiring Diagram**

```
ESP32 DevKitC-V1 Pinout:
╔════════════════════════════════════════════════╗
║  USB Connection:                               ║
║    Micro-USB cable → ESP32 (provides 5V power) ║
║                                                ║
║  Power Output to Rowing Machine:               ║
║    ESP32 3.3V pin → Red wire (sensor VCC)     ║
║    ESP32 GND pin  → Yellow + Black wires      ║
║                                                ║
║  Sensor Input Signals:                         ║
║    GPIO 15 ← White wire (flywheel sensor)     ║
║    GPIO 16 ← Red wire (seat sensor)           ║
║                                                ║
║  GPIO Configuration:                           ║
║    - Mode: INPUT (no internal pull-up/down)    ║
║    - External pull-up via rowing machine      ║
║    - Active LOW detection                      ║
║    - Interrupt on FALLING edge                 ║
║                                                ║
║  Future Expansion Pins (reserved):             ║
║    GPIO 21 → I2C SDA (optional OLED)          ║
║    GPIO 22 → I2C SCL (optional OLED)          ║
║    GPIO 13 → User button / reset session       ║
╚════════════════════════════════════════════════╝
```

## **Sensor Electrical Characteristics**

```
Flywheel Reed Switch (GPIO 15):
  - Voltage HIGH (no magnet): 3.3V (pulled up by machine)
  - Voltage LOW (magnet near): 0V
  - Signal duration when active: ~5-50ms (depends on speed)
  - Max trigger frequency: ~200 Hz (very fast rowing)
  - Debounce time required: 10ms minimum
  - Connection type: Active LOW, external pull-up

Seat Position Reed Switch (GPIO 16):
  - Voltage HIGH (seat not at midpoint): 3.3V
  - Voltage LOW (seat at midpoint): 0V
  - Signal duration: ~50-200ms (depends on stroke rate)
  - Max trigger frequency: ~3 Hz (60 SPM max)
  - Debounce time required: 50ms minimum
  - Connection type: Active LOW, external pull-up

IMPORTANT: Both sensors have external pull-up resistors
in the rowing machine circuit. DO NOT enable ESP32 
internal pull-ups (leave as INPUT, not INPUT_PULLUP).
```

---

# **REFERENCE PROJECTS & ALGORITHMS**

## **Primary Reference: OpenRowingMonitor**

**Repository:** https://github.com/Jannuel-Dizon/openrowingmonitor-ESP32S3-ZephyrOS

**Key Algorithms to Adapt:**

### **1. Physics-Based Rowing Model**

The project uses a physics-based approach rather than simple pulse counting:

```
Core Physics Equations:

1. Angular Velocity (ω):
   ω = 2π / Δt
   where Δt = time between consecutive flywheel pulses (seconds)

2. Angular Acceleration (α):
   α = (ω_current - ω_previous) / Δt

3. Torque on Flywheel:
   τ = I × α
   where I = moment of inertia of flywheel (kg⋅m²)

4. Drag Force (during recovery):
   τ_drag = -k × ω²
   where k = drag coefficient (auto-calibrated)

5. Power Output (during drive phase):
   P = τ_net × ω
   P = (I × α + k × ω²) × ω
   
   In watts, during active rowing:
   P = I × α × ω + k × ω³

6. Work Done per Stroke:
   W = ∫(P dt) during drive phase
   
7. Distance per Stroke:
   d = W / (k × average_velocity)
   
   Simplified for implementation:
   d = k_calibration × (drive_phase_energy)
```

### **2. Stroke Phase Detection**

```
Rowing Stroke Phases:

Drive Phase (pulling):
  - Angular acceleration > threshold (e.g., 10 rad/s²)
  - Angular velocity increasing rapidly
  - Seat sensor triggers (moves from catch to finish)
  - Power is being applied to flywheel
  - Duration: ~0.8-1.2 seconds

Recovery Phase (returning):
  - Angular acceleration < 0 (decelerating)
  - Flywheel spins freely with drag losses
  - Seat returns to front (may not trigger sensor)
  - Duration: ~1.5-2.5 seconds

Detection Algorithm:
1. Monitor angular velocity continuously
2. When velocity crosses threshold (e.g., 15 rad/s) → potential drive start
3. If acceleration > 10 rad/s² → confirm drive phase
4. If seat sensor triggers during acceleration → confirm stroke
5. When velocity peaks and starts decreasing → transition to recovery
6. When velocity drops below minimum threshold → end of recovery
7. Increment stroke count on drive-to-recovery transition
```

### **3. Drag Factor Calibration**

```
Auto-Calibration During Recovery Phases:

During recovery (when no power applied):
  τ_drag = I × α  (all deceleration is due to drag)
  -k × ω² = I × α
  k = -I × α / ω²

Algorithm:
1. Detect clean recovery phase (no re-acceleration)
2. Measure deceleration rate (α) and velocity (ω)
3. Calculate instantaneous drag coefficient: k = -I × α / ω²
4. Filter using exponential moving average:
   k_filtered = 0.95 × k_filtered_old + 0.05 × k_measured
5. Update every recovery phase
6. Use filtered k for all power calculations

Initial values:
  - Moment of inertia (I): 0.101 kg⋅m² (typical rowing machine)
  - Initial drag coefficient (k): 0.0001 (will auto-adjust)
```

### **4. Distance and Pace Calculation**

```
From OpenRowingMonitor approach:

Distance Calculation:
1. Calculate work done per stroke (in joules):
   W = ∫(P dt) over drive phase
   
2. Convert work to distance using calibration factor:
   distance_meters = W / drag_factor_effective
   
3. Accumulate total distance:
   total_distance += distance_meters

Pace Calculation (time per 500m):
1. Current pace (instantaneous):
   pace_500m = 500 / (distance_per_second × 60)
   
2. Average pace:
   pace_avg = (elapsed_time_seconds / total_distance_meters) × 500
   
3. Format as MM:SS.s

Calibration Factor:
- Initial estimate: 2.8 meters per flywheel revolution
- Refine through user calibration:
  * User rows exactly 500m on a Concept2 (reference)
  * System calculates distance using physics model
  * Adjustment factor = 500 / calculated_distance
  * Store in NVS, apply to all future calculations
```

---

# **DETAILED SOFTWARE REQUIREMENTS**

## **ESP-IDF Project Structure**

```
rowing-monitor/
├── CMakeLists.txt                    # Root CMake configuration
├── sdkconfig.defaults                # Default Kconfig settings
├── partitions.csv                    # Custom partition table (if needed)
├── README.md                         # Comprehensive documentation
├── LICENSE                           # Open source license
│
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml             # Component dependencies
│   │
│   ├── main.c                        # Application entry point
│   ├── app_config.h                  # Global configuration defines
│   │
│   ├── sensor_manager.c              # GPIO interrupt handling
│   ├── sensor_manager.h
│   │
│   ├── rowing_physics.c              # Core physics calculations
│   ├── rowing_physics.h
│   │
│   ├── stroke_detector.c             # Stroke phase detection logic
│   ├── stroke_detector.h
│   │
│   ├── metrics_calculator.c          # Distance, pace, power, calories
│   ├── metrics_calculator.h
│   │
│   ├── ble_ftms_server.c            # Bluetooth FTMS service
│   ├── ble_ftms_server.h
│   │
│   ├── wifi_manager.c                # WiFi AP/STA management
│   ├── wifi_manager.h
│   │
│   ├── web_server.c                  # HTTP + WebSocket server
│   ├── web_server.h
│   │
│   ├── config_manager.c              # NVS persistent storage
│   ├── config_manager.h
│   │
│   ├── session_manager.c             # Session tracking, history
│   ├── session_manager.h
│   │
│   ├── utils.c                       # Helper functions
│   ├── utils.h
│   │
│   └── web_content/                  # Embedded web files
│       ├── index.html
│       ├── style.css
│       ├── app.js
│       └── favicon.ico
│
└── components/                       # External components (if any)
    └── (none required, use ESP-IDF components)
```

---

## **DATA STRUCTURES**

### **Core Metrics Structure**

```c
/**
 * @file rowing_physics.h
 * @brief Core data structures for rowing metrics
 */

#ifndef ROWING_PHYSICS_H
#define ROWING_PHYSICS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Stroke phase enumeration
 */
typedef enum {
    STROKE_PHASE_IDLE = 0,      // No activity
    STROKE_PHASE_DRIVE,         // Pulling (power application)
    STROKE_PHASE_RECOVERY       // Returning (flywheel coasting)
} stroke_phase_t;

/**
 * Main rowing metrics structure
 * All fields protected by metrics_mutex
 */
typedef struct {
    // ============ Timing ============
    int64_t session_start_time_us;      // Session start (esp_timer_get_time)
    int64_t last_update_time_us;        // Last metrics update
    uint32_t elapsed_time_ms;           // Total elapsed time in session
    
    // ============ Raw Sensor Data ============
    volatile uint32_t flywheel_pulse_count;    // Total flywheel pulses
    volatile int64_t last_flywheel_time_us;    // Last pulse timestamp
    int64_t prev_flywheel_time_us;             // Previous pulse timestamp
    
    volatile uint32_t seat_trigger_count;      // Total seat sensor triggers
    volatile int64_t last_seat_time_us;        // Last seat trigger timestamp
    
    // ============ Flywheel Physics ============
    float angular_velocity_rad_s;       // Current ω (rad/s)
    float prev_angular_velocity_rad_s;  // Previous ω for acceleration calc
    float angular_acceleration_rad_s2;  // Current α (rad/s²)
    float peak_velocity_in_stroke;      // Peak velocity during current stroke
    
    // ============ Drag Model ============
    float drag_coefficient;             // k value (auto-calibrated)
    float moment_of_inertia;            // I (kg⋅m²), configurable
    float drag_factor;                  // Concept2-style drag factor (100-200)
    uint32_t drag_calibration_samples;  // Number of samples used
    
    // ============ Stroke Detection ============
    stroke_phase_t current_phase;       // Current stroke phase
    uint32_t stroke_count;              // Total strokes in session
    int64_t last_stroke_start_time_us;  // When last stroke started
    int64_t last_stroke_end_time_us;    // When last stroke ended
    float stroke_rate_spm;              // Strokes per minute
    float avg_stroke_rate_spm;          // Average stroke rate
    uint32_t drive_phase_duration_ms;   // Last drive duration
    uint32_t recovery_phase_duration_ms;// Last recovery duration
    
    // ============ Power & Energy ============
    float instantaneous_power_watts;    // Current power output
    float average_power_watts;          // Average power in session
    float peak_power_watts;             // Peak power achieved
    float total_work_joules;            // Total work done (cumulative)
    float drive_phase_work_joules;      // Work in current/last drive phase
    
    // ============ Distance & Pace ============
    float total_distance_meters;        // Total distance rowed
    float instantaneous_pace_sec_500m;  // Current pace (sec per 500m)
    float average_pace_sec_500m;        // Average pace for session
    float best_pace_sec_500m;           // Best pace achieved
    float distance_per_stroke_meters;   // Average distance per stroke
    
    // ============ Calories ============
    uint32_t total_calories;            // Total energy expenditure (kcal)
    float calories_per_hour;            // Current calorie burn rate
    
    // ============ Flags ============
    bool is_active;                     // Currently rowing (vs idle)
    bool calibration_complete;          // Drag factor calibrated
    bool valid_data;                    // Data is valid for display
    
} rowing_metrics_t;

/**
 * Configuration structure (stored in NVS)
 */
typedef struct {
    // ============ Physics Parameters ============
    float moment_of_inertia;            // Default: 0.101 kg⋅m²
    float initial_drag_coefficient;     // Default: 0.0001
    
    // ============ Calibration ============
    float distance_calibration_factor;  // Multiplier for distance calc
    bool auto_calibrate_drag;           // Enable auto-calibration
    uint32_t calibration_row_count;     // Strokes before calibration locked
    
    // ============ User Settings ============
    float user_weight_kg;               // For calorie calculation (default 75)
    uint8_t user_age;                   // Optional, for HR-based calories
    
    // ============ Thresholds ============
    float drive_start_threshold_rad_s;  // Min velocity for drive (default 15)
    float drive_accel_threshold_rad_s2; // Min accel for drive (default 10)
    float recovery_threshold_rad_s;     // Max velocity for recovery (default 8)
    uint32_t idle_timeout_ms;           // Inactivity timeout (default 5000)
    
    // ============ Network ============
    char wifi_ssid[32];                 // WiFi SSID
    char wifi_password[64];             // WiFi password
    char device_name[32];               // BLE device name (default "Crivit Rower")
    bool wifi_enabled;                  // Enable WiFi (default true)
    bool ble_enabled;                   // Enable BLE (default true)
    
    // ============ Display ============
    bool show_power;                    // Show power on web UI
    bool show_calories;                 // Show calories
    char units[8];                      // "metric" or "imperial"
    
} config_t;

/**
 * Session history entry (for storage)
 */
typedef struct {
    uint32_t session_id;
    int64_t start_timestamp;            // Unix timestamp
    uint32_t duration_seconds;
    float total_distance_meters;
    float average_pace_sec_500m;
    float average_power_watts;
    uint32_t stroke_count;
    uint32_t total_calories;
    float drag_factor;
} session_record_t;

#endif // ROWING_PHYSICS_H
```

---

## **SENSOR INTERRUPT HANDLING**

### **sensor_manager.c Implementation Requirements**

```c
/**
 * @file sensor_manager.c
 * @brief GPIO interrupt handlers with debouncing
 * 
 * Critical Performance Requirements:
 * - ISR execution time < 50 microseconds
 * - No blocking operations in ISR
 * - Thread-safe counter increments
 * - Microsecond-precision timestamps
 */

#include "sensor_manager.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

// Pin definitions
#define FLYWHEEL_SENSOR_PIN    GPIO_NUM_15
#define SEAT_SENSOR_PIN        GPIO_NUM_16

// Debounce times (microseconds)
#define FLYWHEEL_DEBOUNCE_US   10000    // 10ms
#define SEAT_DEBOUNCE_US       50000    // 50ms

// Global volatile counters (accessed from ISR and main tasks)
static volatile uint32_t g_flywheel_pulse_count = 0;
static volatile int64_t g_last_flywheel_time_us = 0;

static volatile uint32_t g_seat_trigger_count = 0;
static volatile int64_t g_last_seat_time_us = 0;

// Event group for signaling tasks
static EventGroupHandle_t sensor_event_group;
#define FLYWHEEL_EVENT_BIT  BIT0
#define SEAT_EVENT_BIT      BIT1

/**
 * Flywheel sensor ISR
 * 
 * CRITICAL: Keep this as fast as possible
 * - Read timestamp
 * - Simple debounce check
 * - Increment counter
 * - Set event bit
 * - NO logging, NO complex math
 */
static void IRAM_ATTR flywheel_isr_handler(void* arg) {
    int64_t now = esp_timer_get_time();
    
    // Debounce: ignore if too soon after last pulse
    if ((now - g_last_flywheel_time_us) > FLYWHEEL_DEBOUNCE_US) {
        g_last_flywheel_time_us = now;
        g_flywheel_pulse_count++;
        
        // Signal processing task (non-blocking)
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xEventGroupSetBitsFromISR(sensor_event_group, FLYWHEEL_EVENT_BIT, 
                                  &xHigherPriorityTaskWoken);
        
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

/**
 * Seat position sensor ISR
 */
static void IRAM_ATTR seat_isr_handler(void* arg) {
    int64_t now = esp_timer_get_time();
    
    // Debounce
    if ((now - g_last_seat_time_us) > SEAT_DEBOUNCE_US) {
        g_last_seat_time_us = now;
        g_seat_trigger_count++;
        
        // Signal processing task
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xEventGroupSetBitsFromISR(sensor_event_group, SEAT_EVENT_BIT,
                                  &xHigherPriorityTaskWoken);
        
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

/**
 * Initialize sensor GPIO and interrupts
 */
esp_err_t sensor_manager_init(void) {
    esp_err_t ret;
    
    // Create event group
    sensor_event_group = xEventGroupCreate();
    if (sensor_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor event group");
        return ESP_FAIL;
    }
    
    // Configure flywheel sensor pin
    gpio_config_t flywheel_conf = {
        .pin_bit_mask = (1ULL << FLYWHEEL_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,  // External pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE      // Trigger on falling edge (HIGH→LOW)
    };
    ret = gpio_config(&flywheel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure flywheel GPIO");
        return ret;
    }
    
    // Configure seat sensor pin
    gpio_config_t seat_conf = {
        .pin_bit_mask = (1ULL << SEAT_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,  // External pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    ret = gpio_config(&seat_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure seat GPIO");
        return ret;
    }
    
    // Install GPIO ISR service
    ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means already installed, which is OK
        ESP_LOGE(TAG, "Failed to install ISR service");
        return ret;
    }
    
    // Hook up interrupt handlers
    gpio_isr_handler_add(FLYWHEEL_SENSOR_PIN, flywheel_isr_handler, NULL);
    gpio_isr_handler_add(SEAT_SENSOR_PIN, seat_isr_handler, NULL);
    
    ESP_LOGI(TAG, "Sensor manager initialized");
    ESP_LOGI(TAG, "Flywheel sensor: GPIO%d (active LOW)", FLYWHEEL_SENSOR_PIN);
    ESP_LOGI(TAG, "Seat sensor: GPIO%d (active LOW)", SEAT_SENSOR_PIN);
    
    return ESP_OK;
}

/**
 * Sensor processing task
 * Waits for events from ISR, performs detailed processing
 */
void sensor_processing_task(void *arg) {
    rowing_metrics_t *metrics = (rowing_metrics_t*)arg;
    
    ESP_LOGI(TAG, "Sensor processing task started");
    
    while (1) {
        // Wait for sensor events (block until event or timeout)
        EventBits_t bits = xEventGroupWaitBits(
            sensor_event_group,
            FLYWHEEL_EVENT_BIT | SEAT_EVENT_BIT,
            pdTRUE,  // Clear bits on exit
            pdFALSE, // Don't wait for all bits
            pdMS_TO_TICKS(100)  // 100ms timeout
        );
        
        if (bits & FLYWHEEL_EVENT_BIT) {
            // Flywheel pulse detected
            // Pass to physics calculator
            rowing_physics_process_flywheel_pulse(metrics);
        }
        
        if (bits & SEAT_EVENT_BIT) {
            // Seat trigger detected
            // Pass to stroke detector
            stroke_detector_process_seat_trigger(metrics);
        }
        
        // Check for idle timeout
        int64_t now = esp_timer_get_time();
        int64_t time_since_last_pulse = now - g_last_flywheel_time_us;
        if (time_since_last_pulse > (5000000)) {  // 5 seconds
            // Mark as idle
            if (metrics->is_active) {
                metrics->is_active = false;
                ESP_LOGI(TAG, "Rowing stopped (idle timeout)");
            }
        } else {
            if (!metrics->is_active) {
                metrics->is_active = true;
                ESP_LOGI(TAG, "Rowing started");
            }
        }
    }
}

// Accessor functions for ISR data (thread-safe reads)
uint32_t sensor_get_flywheel_count(void) {
    return g_flywheel_pulse_count;
}

int64_t sensor_get_last_flywheel_time(void) {
    return g_last_flywheel_time_us;
}

uint32_t sensor_get_seat_count(void) {
    return g_seat_trigger_count;
}

int64_t sensor_get_last_seat_time(void) {
    return g_last_seat_time_us;
}
```

---

## **PHYSICS CALCULATION ENGINE**

### **rowing_physics.c Implementation Requirements**

```c
/**
 * @file rowing_physics.c
 * @brief Core physics calculations based on OpenRowingMonitor algorithms
 * 
 * References:
 * - OpenRowingMonitor: github.com/Jannuel-Dizon/openrowingmonitor-ESP32S3-ZephyrOS
 * - Concept2 physics model
 * - Standard rowing machine mechanics
 */

#include "rowing_physics.h"
#include "esp_log.h"
#include <math.h>

#define TAG "PHYSICS"

// Physical constants
#define TWO_PI              6.283185307179586f
#define GRAVITY             9.81f               // m/s²

// Default physics parameters
#define DEFAULT_MOMENT_OF_INERTIA   0.101f      // kg⋅m²
#define DEFAULT_DRAG_COEFFICIENT    0.0001f     // Initial estimate
#define DEFAULT_DISTANCE_PER_REV    2.8f        // meters (calibrate)

/**
 * Initialize physics engine with default values
 */
void rowing_physics_init(rowing_metrics_t *metrics, const config_t *config) {
    memset(metrics, 0, sizeof(rowing_metrics_t));
    
    metrics->session_start_time_us = esp_timer_get_time();
    metrics->moment_of_inertia = config->moment_of_inertia;
    metrics->drag_coefficient = config->initial_drag_coefficient;
    metrics->current_phase = STROKE_PHASE_IDLE;
    metrics->best_pace_sec_500m = 999999.0f;  // Infinity
    
    ESP_LOGI(TAG, "Physics engine initialized");
    ESP_LOGI(TAG, "Moment of inertia: %.4f kg⋅m²", metrics->moment_of_inertia);
}

/**
 * Process new flywheel pulse
 * Called from sensor task when pulse detected
 */
void rowing_physics_process_flywheel_pulse(rowing_metrics_t *metrics) {
    int64_t current_time_us = sensor_get_last_flywheel_time();
    int64_t previous_time_us = metrics->last_flywheel_time_us;
    
    // Skip first pulse (no delta time yet)
    if (previous_time_us == 0) {
        metrics->last_flywheel_time_us = current_time_us;
        return;
    }
    
    // Calculate time delta (seconds)
    float delta_time_s = (current_time_us - previous_time_us) / 1000000.0f;
    
    // Sanity check: ignore if delta time too short or too long
    if (delta_time_s < 0.001f || delta_time_s > 10.0f) {
        ESP_LOGW(TAG, "Invalid delta time: %.6f s", delta_time_s);
        return;
    }
    
    // Calculate angular velocity (rad/s)
    // Assumes 1 pulse per revolution = 2π radians
    float angular_velocity = TWO_PI / delta_time_s;
    
    // Calculate angular acceleration (rad/s²)
    float angular_acceleration = 0.0f;
    if (metrics->prev_angular_velocity_rad_s > 0) {
        angular_acceleration = (angular_velocity - metrics->prev_angular_velocity_rad_s) 
                             / delta_time_s;
    }
    
    // Update metrics
    metrics->prev_angular_velocity_rad_s = metrics->angular_velocity_rad_s;
    metrics->angular_velocity_rad_s = angular_velocity;
    metrics->angular_acceleration_rad_s2 = angular_acceleration;
    metrics->prev_flywheel_time_us = previous_time_us;
    metrics->last_flywheel_time_us = current_time_us;
    metrics->flywheel_pulse_count++;
    metrics->last_update_time_us = esp_timer_get_time();
    
    // Track peak velocity in current stroke
    if (angular_velocity > metrics->peak_velocity_in_stroke) {
        metrics->peak_velocity_in_stroke = angular_velocity;
    }
    
    // Update drag calibration if in recovery phase
    if (metrics->current_phase == STROKE_PHASE_RECOVERY && 
        angular_acceleration < 0) {
        // Only use clean deceleration samples
        rowing_physics_calibrate_drag(metrics, angular_velocity, angular_acceleration);
    }
    
    // Calculate instantaneous power
    rowing_physics_calculate_power(metrics);
    
    // Log for debugging (only every 10th pulse to avoid spam)
    if (metrics->flywheel_pulse_count % 10 == 0) {
        ESP_LOGD(TAG, "ω=%.2f rad/s, α=%.2f rad/s², P=%.1f W", 
                 angular_velocity, angular_acceleration, 
                 metrics->instantaneous_power_watts);
    }
}

/**
 * Auto-calibrate drag coefficient during recovery phases
 */
void rowing_physics_calibrate_drag(rowing_metrics_t *metrics, 
                                   float omega, float alpha) {
    // During recovery: τ_drag = I × α = -k × ω²
    // Solve for k: k = -I × α / ω²
    
    if (fabsf(omega) < 1.0f) {
        return;  // Velocity too low, avoid division issues
    }
    
    float measured_k = -metrics->moment_of_inertia * alpha / (omega * omega);
    
    // Sanity check
    if (measured_k < 0 || measured_k > 0.01f) {
        return;  // Invalid measurement
    }
    
    // Exponential moving average filter
    float alpha_filter = 0.05f;  // 5% new, 95% old
    if (metrics->drag_calibration_samples == 0) {
        // First sample
        metrics->drag_coefficient = measured_k;
    } else {
        metrics->drag_coefficient = (1.0f - alpha_filter) * metrics->drag_coefficient 
                                   + alpha_filter * measured_k;
    }
    
    metrics->drag_calibration_samples++;
    
    // Convert to Concept2-style drag factor (100-200 range)
    // Drag factor = 1000 / k (approximately)
    metrics->drag_factor = 1000.0f / metrics->drag_coefficient;
    
    // Mark calibration complete after 50 samples
    if (metrics->drag_calibration_samples >= 50 && !metrics->calibration_complete) {
        metrics->calibration_complete = true;
        ESP_LOGI(TAG, "Drag calibration complete: k=%.6f, DF=%.1f", 
                 metrics->drag_coefficient, metrics->drag_factor);
    }
}

/**
 * Calculate instantaneous power
 */
void rowing_physics_calculate_power(rowing_metrics_t *metrics) {
    float omega = metrics->angular_velocity_rad_s;
    float alpha = metrics->angular_acceleration_rad_s2;
    float I = metrics->moment_of_inertia;
    float k = metrics->drag_coefficient;
    
    // Power = (I × α + k × ω²) × ω
    // First term: power to accelerate flywheel
    // Second term: power to overcome drag
    
    float accel_power = I * alpha * omega;
    float drag_power = k * omega * omega * omega;
    float total_power = accel_power + drag_power;
    
    // Clamp to reasonable range
    if (total_power < 0) total_power = 0;
    if (total_power > 2000) total_power = 2000;  // Max human power ~2kW
    
    metrics->instantaneous_power_watts = total_power;
    
    // Update peak power
    if (total_power > metrics->peak_power_watts) {
        metrics->peak_power_watts = total_power;
    }
    
    // Update average power (only during active rowing)
    if (metrics->current_phase == STROKE_PHASE_DRIVE && total_power > 10.0f) {
        uint32_t n = metrics->stroke_count;
        if (n == 0) n = 1;
        metrics->average_power_watts = (metrics->average_power_watts * (n - 1) + total_power) / n;
    }
}

/**
 * Calculate distance for completed stroke
 */
void rowing_physics_calculate_distance(rowing_metrics_t *metrics) {
    // Simple method: use calibration factor × flywheel revolutions
    // More complex method would integrate work done
    
    // For now, increment by calibration factor per stroke
    float distance_this_stroke = DEFAULT_DISTANCE_PER_REV;  // Will be replaced by calibrated value
    
    metrics->total_distance_meters += distance_this_stroke;
    metrics->distance_per_stroke_meters = distance_this_stroke;
    
    // Update pace calculations
    rowing_physics_calculate_pace(metrics);
}

/**
 * Calculate pace (time per 500m)
 */
void rowing_physics_calculate_pace(rowing_metrics_t *metrics) {
    int64_t elapsed_us = esp_timer_get_time() - metrics->session_start_time_us;
    float elapsed_s = elapsed_us / 1000000.0f;
    
    if (metrics->total_distance_meters < 1.0f) {
        metrics->instantaneous_pace_sec_500m = 999999.0f;
        metrics->average_pace_sec_500m = 999999.0f;
        return;
    }
    
    // Average pace for entire session
    metrics->average_pace_sec_500m = (elapsed_s / metrics->total_distance_meters) * 500.0f;
    
    // Instantaneous pace (based on last few strokes)
    // Use a rolling window of last 5 strokes
    // For simplicity, use average pace for now
    // TODO: Implement rolling window
    metrics->instantaneous_pace_sec_500m = metrics->average_pace_sec_500m;
    
    // Update best pace
    if (metrics->instantaneous_pace_sec_500m < metrics->best_pace_sec_500m) {
        metrics->best_pace_sec_500m = metrics->instantaneous_pace_sec_500m;
    }
}

/**
 * Calculate calories burned
 * Based on power output and time
 */
void rowing_physics_calculate_calories(rowing_metrics_t *metrics, float user_weight_kg) {
    // Mechanical efficiency ~20-25% for rowing
    // 1 watt = 0.01433 kcal/min (approximately)
    
    int64_t elapsed_us = esp_timer_get_time() - metrics->session_start_time_us;
    float elapsed_min = elapsed_us / 60000000.0f;
    
    float avg_power = metrics->average_power_watts;
    float calories = avg_power * 0.01433f * elapsed_min;
    
    metrics->total_calories = (uint32_t)calories;
    
    // Calories per hour (current rate)
    if (elapsed_min > 0) {
        metrics->calories_per_hour = calories * (60.0f / elapsed_min);
    }
}

/**
 * Format pace as MM:SS string
 */
void rowing_physics_format_pace(float pace_seconds, char *buffer, size_t buf_len) {
    if (pace_seconds > 9999.0f) {
        snprintf(buffer, buf_len, "--:--");
        return;
    }
    
    uint32_t minutes = (uint32_t)(pace_seconds / 60.0f);
    uint32_t seconds = (uint32_t)pace_seconds % 60;
    uint32_t tenths = (uint32_t)((pace_seconds - (uint32_t)pace_seconds) * 10.0f);
    
    snprintf(buffer, buf_len, "%02u:%02u.%01u", minutes, seconds, tenths);
}
```

---

## **STROKE DETECTION**

### **stroke_detector.c Implementation Requirements**

```c
/**
 * @file stroke_detector.c
 * @brief Stroke phase detection using flywheel velocity patterns and seat sensor
 */

#include "stroke_detector.h"
#include "esp_log.h"

#define TAG "STROKE"

// Thresholds (configurable)
#define DRIVE_START_VELOCITY_THRESHOLD      15.0f   // rad/s
#define DRIVE_ACCELERATION_THRESHOLD        10.0f   // rad/s²
#define RECOVERY_VELOCITY_THRESHOLD         8.0f    // rad/s
#define MINIMUM_STROKE_DURATION_MS          500     // Minimum time for valid stroke

/**
 * Process stroke phase detection
 * Called continuously from main rowing task
 */
void stroke_detector_update(rowing_metrics_t *metrics) {
    float omega = metrics->angular_velocity_rad_s;
    float alpha = metrics->angular_acceleration_rad_s2;
    stroke_phase_t current_phase = metrics->current_phase;
    int64_t now = esp_timer_get_time();
    
    switch (current_phase) {
        case STROKE_PHASE_IDLE:
            // Check for drive start
            if (omega > DRIVE_START_VELOCITY_THRESHOLD && 
                alpha > DRIVE_ACCELERATION_THRESHOLD) {
                // Transition to drive
                metrics->current_phase = STROKE_PHASE_DRIVE;
                metrics->last_stroke_start_time_us = now;
                metrics->peak_velocity_in_stroke = omega;
                ESP_LOGD(TAG, "Drive phase started");
            }
            break;
            
        case STROKE_PHASE_DRIVE:
            // Check for recovery transition
            if (alpha < 0 && omega < metrics->peak_velocity_in_stroke * 0.9f) {
                // Velocity peaked and now decreasing → end of drive
                metrics->current_phase = STROKE_PHASE_RECOVERY;
                metrics->last_stroke_end_time_us = now;
                
                uint32_t drive_duration_ms = (now - metrics->last_stroke_start_time_us) / 1000;
                metrics->drive_phase_duration_ms = drive_duration_ms;
                
                // Increment stroke count if duration valid
                if (drive_duration_ms >= MINIMUM_STROKE_DURATION_MS) {
                    metrics->stroke_count++;
                    
                    // Calculate stroke rate
                    stroke_detector_calculate_stroke_rate(metrics);
                    
                    // Calculate distance for this stroke
                    rowing_physics_calculate_distance(metrics);
                    
                    ESP_LOGI(TAG, "Stroke #%u complete, SPM=%.1f, dist=%.1fm", 
                             metrics->stroke_count, metrics->stroke_rate_spm,
                             metrics->total_distance_meters);
                }
            }
            break;
            
        case STROKE_PHASE_RECOVERY:
            // Check for next drive or idle
            if (omega < RECOVERY_VELOCITY_THRESHOLD) {
                // Very slow, transition to idle
                metrics->current_phase = STROKE_PHASE_IDLE;
                metrics->peak_velocity_in_stroke = 0;
            } else if (alpha > DRIVE_ACCELERATION_THRESHOLD) {
                // Re-acceleration detected, new stroke starting
                metrics->current_phase = STROKE_PHASE_DRIVE;
                metrics->last_stroke_start_time_us = now;
                metrics->peak_velocity_in_stroke = omega;
            }
            break;
    }
}

/**
 * Process seat sensor trigger
 * Called from sensor task when seat sensor activates
 */
void stroke_detector_process_seat_trigger(rowing_metrics_t *metrics) {
    // Seat sensor triggers at mid-rail (catch-to-finish transition)
    // This is a strong indicator of drive phase
    
    stroke_phase_t current_phase = metrics->current_phase;
    
    if (current_phase == STROKE_PHASE_IDLE || current_phase == STROKE_PHASE_RECOVERY) {
        // Seat trigger confirms drive is happening
        // Force transition to drive phase
        metrics->current_phase = STROKE_PHASE_DRIVE;
        metrics->last_stroke_start_time_us = esp_timer_get_time();
        
        ESP_LOGD(TAG, "Drive phase confirmed by seat sensor");
    }
}

/**
 * Calculate stroke rate (strokes per minute)
 */
void stroke_detector_calculate_stroke_rate(rowing_metrics_t *metrics) {
    if (metrics->stroke_count < 2) {
        metrics->stroke_rate_spm = 0;
        return;
    }
    
    // Calculate from last few strokes (exponential moving average)
    int64_t time_between_strokes_us = metrics->last_stroke_end_time_us - 
                                      (metrics->last_stroke_end_time_us - 
                                       metrics->drive_phase_duration_ms * 1000 - 
                                       metrics->recovery_phase_duration_ms * 1000);
    
    float time_between_strokes_min = time_between_strokes_us / 60000000.0f;
    float instantaneous_spm = 1.0f / time_between_strokes_min;
    
    // Filter
    if (metrics->stroke_rate_spm == 0) {
        metrics->stroke_rate_spm = instantaneous_spm;
    } else {
        metrics->stroke_rate_spm = 0.7f * metrics->stroke_rate_spm + 0.3f * instantaneous_spm;
    }
    
    // Calculate average for entire session
    int64_t elapsed_us = esp_timer_get_time() - metrics->session_start_time_us;
    float elapsed_min = elapsed_us / 60000000.0f;
    if (elapsed_min > 0) {
        metrics->avg_stroke_rate_spm = metrics->stroke_count / elapsed_min;
    }
}
```

---

## **BLUETOOTH FTMS SERVICE**

### **ble_ftms_server.c Implementation Requirements**

```c
/**
 * @file ble_ftms_server.c
 * @brief Bluetooth Low Energy Fitness Machine Service (FTMS) implementation
 * 
 * Specification: Bluetooth SIG FTMS 1.0
 * Service UUID: 0x1826
 * Rower Data Characteristic UUID: 0x2AD1
 * 
 * Compatible with apps: Kinomap, EXR, MyHomeFit, Concept2 ErgData, etc.
 */

#include "ble_ftms_server.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"

#define TAG "BLE_FTMS"

// FTMS Service and Characteristic UUIDs
static const ble_uuid128_t FTMS_SERVICE_UUID = 
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x26, 0x18, 0x00, 0x00);

static const ble_uuid128_t ROWER_DATA_UUID =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0xd1, 0x2a, 0x00, 0x00);

// Connection handle
static uint16_t g_conn_handle = 0;
static bool g_connected = false;

// Notification state
static bool g_notify_enabled = false;

// Characteristic value handle
static uint16_t g_rower_data_handle;

/**
 * Build FTMS Rower Data packet
 * 
 * Data format (per FTMS spec):
 * Byte 0-1: Flags (uint16) - indicates which fields are present
 * Byte 2-3: Stroke Rate (uint16) - 0.5 strokes/min resolution
 * Byte 4-5: Stroke Count (uint16)
 * Byte 6-8: Total Distance (uint24) - meters
 * Byte 9-10: Instantaneous Pace (uint16) - seconds per 500m
 * Byte 11-12: Average Pace (uint16)
 * Byte 13-14: Instantaneous Power (sint16) - watts
 * Byte 15-16: Average Power (sint16)
 * Byte 17-18: Total Energy (uint16) - kcal
 * Byte 19-20: Energy Per Hour (uint16) - kcal/h
 * Byte 21-22: Elapsed Time (uint16) - seconds
 */
static size_t build_rower_data_packet(const rowing_metrics_t *metrics, uint8_t *packet) {
    size_t offset = 0;
    
    // Flags: indicate all fields present
    uint16_t flags = 0x07FF;  // All fields present
    memcpy(&packet[offset], &flags, 2);
    offset += 2;
    
    // Stroke rate (0.5 SPM resolution)
    uint16_t stroke_rate = (uint16_t)(metrics->stroke_rate_spm * 2.0f);
    memcpy(&packet[offset], &stroke_rate, 2);
    offset += 2;
    
    // Stroke count
    uint16_t stroke_count = (uint16_t)metrics->stroke_count;
    memcpy(&packet[offset], &stroke_count, 2);
    offset += 2;
    
    // Total distance (24-bit, meters)
    uint32_t distance = (uint32_t)metrics->total_distance_meters;
    memcpy(&packet[offset], &distance, 3);
    offset += 3;
    
    // Instantaneous pace (seconds per 500m)
    uint16_t pace = (uint16_t)metrics->instantaneous_pace_sec_500m;
    if (pace > 9999) pace = 9999;
    memcpy(&packet[offset], &pace, 2);
    offset += 2;
    
    // Average pace
    uint16_t avg_pace = (uint16_t)metrics->average_pace_sec_500m;
    if (avg_pace > 9999) avg_pace = 9999;
    memcpy(&packet[offset], &avg_pace, 2);
    offset += 2;
    
    // Instantaneous power (signed, watts)
    int16_t power = (int16_t)metrics->instantaneous_power_watts;
    memcpy(&packet[offset], &power, 2);
    offset += 2;
    
    // Average power
    int16_t avg_power = (int16_t)metrics->average_power_watts;
    memcpy(&packet[offset], &avg_power, 2);
    offset += 2;
    
    // Total energy (kcal)
    uint16_t energy = (uint16_t)metrics->total_calories;
    memcpy(&packet[offset], &energy, 2);
    offset += 2;
    
    // Energy per hour
    uint16_t energy_per_hour = (uint16_t)metrics->calories_per_hour;
    memcpy(&packet[offset], &energy_per_hour, 2);
    offset += 2;
    
    // Elapsed time (seconds)
    uint32_t elapsed_ms = metrics->elapsed_time_ms;
    uint16_t elapsed_s = (uint16_t)(elapsed_ms / 1000);
    memcpy(&packet[offset], &elapsed_s, 2);
    offset += 2;
    
    return offset;
}

/**
 * Send BLE notification with current metrics
 */
esp_err_t ble_ftms_notify_metrics(const rowing_metrics_t *metrics) {
    if (!g_connected || !g_notify_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t packet[30];
    size_t packet_len = build_rower_data_packet(metrics, packet);
    
    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, packet_len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return ESP_ERR_NO_MEM;
    }
    
    int rc = ble_gattc_notify_custom(g_conn_handle, g_rower_data_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to send notification: %d", rc);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/**
 * Initialize BLE FTMS service
 */
esp_err_t ble_ftms_init(const char *device_name) {
    // Initialize NimBLE stack
    nimble_port_init();
    
    // Configure device name
    ble_svc_gap_device_name_set(device_name);
    
    // Start NimBLE host task
    nimble_port_freertos_init(ble_host_task);
    
    ESP_LOGI(TAG, "BLE FTMS initialized: %s", device_name);
    
    return ESP_OK;
}

// Additional BLE implementation details...
// (Full GAP/GATT service registration, advertising, connection handling)
// This is a simplified outline - full implementation would be ~500 lines
```

---

## **WEB SERVER IMPLEMENTATION**

### **web_server.c Requirements**

```c
/**
 * @file web_server.c
 * @brief Async HTTP server with WebSocket for real-time data
 * 
 * Features:
 * - Serves embedded HTML/CSS/JS files
 * - WebSocket for real-time metrics streaming
 * - REST API for configuration
 * - Session control endpoints
 */

#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#define TAG "WEB_SERVER"

static httpd_handle_t g_server = NULL;
static int g_ws_fd = -1;  // WebSocket file descriptor

/**
 * Serve main HTML page
 */
static esp_err_t index_handler(httpd_req_t *req) {
    extern const char index_html_start[] asm("_binary_index_html_start");
    extern const char index_html_end[]   asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_size);
    return ESP_OK;
}

/**
 * API endpoint: Get current metrics as JSON
 */
static esp_err_t api_metrics_handler(httpd_req_t *req) {
    rowing_metrics_t *metrics = (rowing_metrics_t*)req->user_ctx;
    
    // Build JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "distance", metrics->total_distance_meters);
    cJSON_AddNumberToObject(root, "pace", metrics->instantaneous_pace_sec_500m);
    cJSON_AddNumberToObject(root, "avgPace", metrics->average_pace_sec_500m);
    cJSON_AddNumberToObject(root, "power", metrics->instantaneous_power_watts);
    cJSON_AddNumberToObject(root, "avgPower", metrics->average_power_watts);
    cJSON_AddNumberToObject(root, "strokeRate", metrics->stroke_rate_spm);
    cJSON_AddNumberToObject(root, "strokeCount", metrics->stroke_count);
    cJSON_AddNumberToObject(root, "calories", metrics->total_calories);
    cJSON_AddNumberToObject(root, "elapsedTime", metrics->elapsed_time_ms / 1000);
    cJSON_AddBoolToObject(root, "isActive", metrics->is_active);
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    return ESP_OK;
}

/**
 * WebSocket handler for real-time streaming
 */
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // Initial WebSocket handshake
        ESP_LOGI(TAG, "WebSocket connection initiated");
        g_ws_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }
    
    // Handle WebSocket frames (if needed for bidirectional comms)
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Echo or process frame (if needed)
    return ESP_OK;
}

/**
 * Broadcast metrics via WebSocket
 * Called periodically from main task
 */
esp_err_t web_server_broadcast_metrics(const rowing_metrics_t *metrics) {
    if (g_server == NULL || g_ws_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Build JSON
    cJSON *root = cJSON_CreateObject();
    // ... (same as api_metrics_handler)
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    // Send as WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)json_string;
    ws_pkt.len = strlen(json_string);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_send_frame_async(g_server, g_ws_fd, &ws_pkt);
    
    free(json_string);
    return ret;
}

/**
 * Start HTTP server
 */
esp_err_t web_server_start(rowing_metrics_t *metrics) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);
    
    if (httpd_start(&g_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server");
        return ESP_FAIL;
    }
    
    // Register URI handlers
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_server, &index_uri);
    
    httpd_uri_t metrics_uri = {
        .uri = "/api/metrics",
        .method = HTTP_GET,
        .handler = api_metrics_handler,
        .user_ctx = metrics
    };
    httpd_register_uri_handler(g_server, &metrics_uri);
    
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = metrics,
        .is_websocket = true
    };
    httpd_register_uri_handler(g_server, &ws_uri);
    
    ESP_LOGI(TAG, "Web server started successfully");
    return ESP_OK;
}
```

---

## **WEB INTERFACE HTML**

### **web_content/index.html**

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Crivit Rowing Monitor</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #0f3460 100%);
            color: #eee;
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        
        h1 {
            text-align: center;
            margin-bottom: 30px;
            font-size: 2.5em;
            color: #16d9e3;
            text-shadow: 0 0 10px rgba(22, 217, 227, 0.5);
        }
        
        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(auto
