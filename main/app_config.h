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
// Pin defaults come from the board profile selected by Kconfig
// (components/board/Kconfig). Define ROWING_USE_BOARD_PINS=0 in your
// build to fall back to the legacy hard-coded pins below.
#include "board.h"

// Sensor inputs
#ifndef GPIO_FLYWHEEL_SENSOR
#define GPIO_FLYWHEEL_SENSOR    BOARD_FLYWHEEL_PIN  // White wire - flywheel reed switch
#endif
#ifndef GPIO_SEAT_SENSOR
#define GPIO_SEAT_SENSOR        BOARD_SEAT_PIN      // Red wire - seat position reed switch
#endif

// Future expansion (optional OLED) — these are the I2C bus pins shared
// by display, touch, and other I2C peripherals.
#ifndef GPIO_I2C_SDA
#define GPIO_I2C_SDA            BOARD_I2C_SDA
#endif
#ifndef GPIO_I2C_SCL
#define GPIO_I2C_SCL            BOARD_I2C_SCL
#endif

// User button (optional) — kept for backward compatibility. New
// programmable buttons live in components/input_buttons.
#ifndef GPIO_USER_BUTTON
#define GPIO_USER_BUTTON        (BOARD_BTN1_PIN >= 0 ? BOARD_BTN1_PIN : 13)
#endif

// RGB LED (ESP32-S3 DevKitC built-in) — overridden by indicator_led
// component when enabled.
#ifndef GPIO_RGB_LED
#define GPIO_RGB_LED            (BOARD_RGB_LED_PIN >= 0 ? BOARD_RGB_LED_PIN : 48)
#endif

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
// Provisioning softAP password - WPA2 is often more stable than open networks
// on ESP32. Using a simple password helps with client authentication issues.
// Note: Password must be 8-63 characters for WPA2.
#define WIFI_AP_PROV_PASSWORD           "rowing123"     // Password for provisioning softAP
#define WIFI_AP_PASS_DEFAULT            ""              // Default AP password (empty for config)
#define WIFI_AP_CHANNEL                 11              // WiFi channel
#define WIFI_AP_MAX_CONNECTIONS         4

// Delay after starting softAP provisioning to ensure DHCP server is fully initialized
#define WIFI_DHCP_INIT_DELAY_MS         500

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
#define SENSOR_TASK_STACK_SIZE          5120
#define SENSOR_TASK_PRIORITY            10      // High priority for sensor processing

#define METRICS_TASK_STACK_SIZE         6144
#define METRICS_TASK_PRIORITY           5

// Broadcast task allocates JSON + SSE buffers on stack and calls into the
// HTTP server / NimBLE host stacks; 4 KB was previously tight enough that a
// long-running session could overflow the stack and crash silently.
#define BLE_TASK_STACK_SIZE             6144
#define BLE_TASK_PRIORITY               4

#define WEB_TASK_STACK_SIZE             8192
#define WEB_TASK_PRIORITY               3

// ============================================================================
// BUFFER SIZES
// ============================================================================
// JSON buffer for /api/metrics and broadcast — sized to comfortably hold the
// full payload (21+ keys with floats and strings); 512 was on the edge of
// truncation which produced unterminated/invalid JSON intermittently.
#define JSON_BUFFER_SIZE                1024
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
