/**
 * @file app_config.h
 * @brief Global configuration defines for ESP32 Rowing Monitor
 * 
 * This file contains all hardware pin assignments, timing constants,
 * default values, and compile-time configuration options.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// VERSION INFORMATION
// ============================================================================
#define APP_VERSION_MAJOR       1
#define APP_VERSION_MINOR       0
#define APP_VERSION_PATCH       0
#define APP_VERSION_STRING      "1.0.0"

// ============================================================================
// GPIO PIN ASSIGNMENTS
// ============================================================================
// Sensor inputs
#define GPIO_FLYWHEEL_SENSOR    15      // White wire - flywheel reed switch
#define GPIO_SEAT_SENSOR        16      // Red wire - seat position reed switch

// Future expansion (optional OLED)
#define GPIO_I2C_SDA            21
#define GPIO_I2C_SCL            22

// User button (optional)
#define GPIO_USER_BUTTON        13

// RGB LED (ESP32-S3 DevKitC built-in)
#define GPIO_RGB_LED            48

// ============================================================================
// SENSOR TIMING CONFIGURATION
// ============================================================================
// Debounce times in microseconds
#define FLYWHEEL_DEBOUNCE_US    10000   // 10ms debounce for flywheel
#define SEAT_DEBOUNCE_US        50000   // 50ms debounce for seat sensor

// Idle timeout in milliseconds
#define IDLE_TIMEOUT_MS         5000    // 5 seconds without pulses = idle

// Auto-pause timeout - pause recording when no flywheel activity
#define AUTO_PAUSE_TIMEOUT_MS   5000    // 5 seconds without pulses = auto-pause

// Maximum expected flywheel frequency (Hz)
#define MAX_FLYWHEEL_FREQ_HZ    200     // Very fast rowing limit

// ============================================================================
// PHYSICS CONSTANTS
// ============================================================================
// Mathematical constants
#define TWO_PI                  6.283185307179586f
#define GRAVITY_M_S2            9.81f

// Rowing machine physics (default values, can be calibrated)
#define DEFAULT_MOMENT_OF_INERTIA   0.101f      // kg⋅m² (typical rowing machine)
#define DEFAULT_DRAG_COEFFICIENT    0.0001f     // Initial estimate
#define DEFAULT_DISTANCE_PER_REV    2.8f        // meters per flywheel revolution
#define DEFAULT_MAGNETS_PER_REV     4           // Number of magnets on flywheel (1-16)

// ============================================================================
// STROKE DETECTION THRESHOLDS
// ============================================================================
#define DRIVE_START_VELOCITY_THRESHOLD      15.0f   // rad/s minimum to start drive
#define DRIVE_ACCELERATION_THRESHOLD        10.0f   // rad/s² minimum for drive
#define RECOVERY_VELOCITY_THRESHOLD         8.0f    // rad/s maximum for recovery
#define MINIMUM_STROKE_DURATION_MS          500     // Minimum valid stroke time

// ============================================================================
// BLE CONFIGURATION
// ============================================================================
#define BLE_DEVICE_NAME_DEFAULT         "Crivit Rower"
#define BLE_DEVICE_NAME_MAX_LEN         32

// FTMS Update rate
#define BLE_NOTIFY_INTERVAL_MS          500     // Send BLE notifications every 500ms

// BLE HR Client configuration
// Disabled by default - requires these NimBLE options in menuconfig:
//   - CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
//   - CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
//   - CONFIG_BT_NIMBLE_GATT_CLIENT=y
// See README.md for detailed instructions
#define BLE_HR_CLIENT_ENABLED           1       // Set to 1 to enable BLE HR client
#define BLE_HR_SCAN_TIMEOUT_SEC         120     // Scan timeout (matches Heart for Bluetooth pairing window)
#define BLE_HR_CONNECT_TIMEOUT_MS       30000   // Connection timeout (30 seconds)

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================
#define WIFI_AP_SSID_DEFAULT            "CrivitRower"
// ESP32-S3 has known softAP issues with WPA2 (GitHub #13210, #13508).
// WIFI_AUTH_OPEN (no password) often works when WPA2 fails.
// Set password to empty string "" for open network, or 8+ chars for WPA2.
#define WIFI_AP_PASS_DEFAULT            ""              // Open network - most reliable on ESP32-S3
#define WIFI_AP_CHANNEL                 11              // Fallback channel if auto-select fails
#define WIFI_AP_MAX_CONNECTIONS         4

// ============================================================================
// WEB SERVER CONFIGURATION
// ============================================================================
#define WEB_SERVER_PORT                 80
#define WS_BROADCAST_INTERVAL_MS        200     // WebSocket update rate

// ============================================================================
// NVS STORAGE CONFIGURATION
// ============================================================================
#define NVS_NAMESPACE                   "rowing"
#define NVS_KEY_MOMENT_OF_INERTIA       "moi"
#define NVS_KEY_DRAG_COEFF              "drag"
#define NVS_KEY_DISTANCE_CAL            "dist_cal"
#define NVS_KEY_USER_WEIGHT             "weight"
#define NVS_KEY_WIFI_SSID               "wifi_ssid"
#define NVS_KEY_WIFI_PASS               "wifi_pass"
#define NVS_KEY_DEVICE_NAME             "dev_name"

// ============================================================================
// CALORIE CALCULATION
// ============================================================================
#define DEFAULT_USER_WEIGHT_KG          75.0f
#define CALORIES_PER_WATT_MINUTE        0.01433f    // kcal per watt-minute

// ============================================================================
// TASK CONFIGURATION
// ============================================================================
#define SENSOR_TASK_STACK_SIZE          4096
#define SENSOR_TASK_PRIORITY            10      // High priority for sensor processing

#define METRICS_TASK_STACK_SIZE         4096
#define METRICS_TASK_PRIORITY           5

#define BLE_TASK_STACK_SIZE             4096
#define BLE_TASK_PRIORITY               4

#define WEB_TASK_STACK_SIZE             8192
#define WEB_TASK_PRIORITY               3

// ============================================================================
// BUFFER SIZES
// ============================================================================
#define JSON_BUFFER_SIZE                512
#define PACE_STRING_BUFFER_SIZE         16

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
#ifdef CONFIG_LOG_MAXIMUM_LEVEL_DEBUG
#define DEBUG_LOG_EVERY_N_PULSES        10      // Log debug info every N pulses
#else
#define DEBUG_LOG_EVERY_N_PULSES        100
#endif

#endif // APP_CONFIG_H
