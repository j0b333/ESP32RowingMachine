/**
 * @file rowing_physics.h
 * @brief Core data structures and physics engine for rowing metrics
 * 
 * This file defines the main data structures used throughout the application
 * for tracking rowing metrics, configuration, and session data.
 */

#ifndef ROWING_PHYSICS_H
#define ROWING_PHYSICS_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
 * All fields should be protected by metrics_mutex when accessed from multiple tasks
 */
typedef struct {
    // ============ Timing ============
    int64_t session_start_time_us;      // Session start timestamp (esp_timer_get_time)
    int64_t last_update_time_us;        // Last metrics update timestamp
    uint32_t elapsed_time_ms;           // Total elapsed time in current session
    
    // ============ Raw Sensor Data ============
    volatile uint32_t flywheel_pulse_count;    // Total flywheel pulses in session
    volatile int64_t last_flywheel_time_us;    // Timestamp of last flywheel pulse
    int64_t prev_flywheel_time_us;             // Timestamp of previous flywheel pulse
    
    volatile uint32_t seat_trigger_count;      // Total seat sensor triggers
    volatile int64_t last_seat_time_us;        // Timestamp of last seat trigger
    
    // ============ Flywheel Physics ============
    float angular_velocity_rad_s;       // Current angular velocity (rad/s)
    float prev_angular_velocity_rad_s;  // Previous angular velocity for acceleration
    float angular_acceleration_rad_s2;  // Current angular acceleration (rad/s²)
    float peak_velocity_in_stroke;      // Peak velocity during current stroke
    
    // ============ Drag Model ============
    float drag_coefficient;             // k value (auto-calibrated)
    float moment_of_inertia;            // I (kg⋅m²), configurable
    float drag_factor;                  // Concept2-style drag factor (100-200 range)
    uint32_t drag_calibration_samples;  // Number of calibration samples collected
    
    // ============ Stroke Detection ============
    stroke_phase_t current_phase;       // Current stroke phase
    uint32_t stroke_count;              // Total strokes in session
    int64_t last_stroke_start_time_us;  // When last stroke started
    int64_t last_stroke_end_time_us;    // When last stroke ended
    float stroke_rate_spm;              // Current strokes per minute
    float avg_stroke_rate_spm;          // Average stroke rate for session
    uint32_t drive_phase_duration_ms;   // Last drive phase duration
    uint32_t recovery_phase_duration_ms;// Last recovery phase duration
    
    // ============ Power & Energy ============
    float instantaneous_power_watts;    // Current power output
    float average_power_watts;          // Average power for session
    float peak_power_watts;             // Peak power achieved
    float total_work_joules;            // Total work done (cumulative)
    float drive_phase_work_joules;      // Work in current/last drive phase
    
    // ============ Distance & Pace ============
    float total_distance_meters;        // Total distance rowed
    float instantaneous_pace_sec_500m;  // Current pace (seconds per 500m)
    float average_pace_sec_500m;        // Average pace for session
    float best_pace_sec_500m;           // Best pace achieved
    float distance_per_stroke_meters;   // Average distance per stroke
    
    // ============ Calories ============
    uint32_t total_calories;            // Total energy expenditure (kcal)
    float calories_per_hour;            // Current calorie burn rate
    
    // ============ Flags ============
    bool is_active;                     // Currently rowing (vs idle)
    bool calibration_complete;          // Drag factor calibration done
    bool valid_data;                    // Data is valid for display
    
} rowing_metrics_t;

/**
 * Configuration structure (stored in NVS)
 */
typedef struct {
    // ============ Physics Parameters ============
    float moment_of_inertia;            // Default: 0.101 kg⋅m²
    float initial_drag_coefficient;     // Default: 0.0001
    float distance_calibration_factor;  // Multiplier for distance calculation
    
    // ============ Calibration Settings ============
    bool auto_calibrate_drag;           // Enable automatic drag calibration
    uint32_t calibration_row_count;     // Strokes before calibration locked
    
    // ============ User Settings ============
    float user_weight_kg;               // User weight for calorie calculation
    uint8_t user_age;                   // User age (optional, for HR-based calories)
    
    // ============ Detection Thresholds ============
    float drive_start_threshold_rad_s;  // Min velocity for drive detection
    float drive_accel_threshold_rad_s2; // Min acceleration for drive detection
    float recovery_threshold_rad_s;     // Max velocity for recovery detection
    uint32_t idle_timeout_ms;           // Inactivity timeout
    
    // ============ Network Settings ============
    char wifi_ssid[32];                 // WiFi SSID (AP mode or STA)
    char wifi_password[64];             // WiFi password
    char device_name[32];               // BLE device name
    bool wifi_enabled;                  // Enable WiFi subsystem
    bool ble_enabled;                   // Enable BLE subsystem
    
    // ============ Display Settings ============
    bool show_power;                    // Show power on web UI
    bool show_calories;                 // Show calories on web UI
    char units[8];                      // "metric" or "imperial"
    
} config_t;

/**
 * Session history entry (for storage)
 */
typedef struct {
    uint32_t session_id;                // Unique session identifier
    int64_t start_timestamp;            // Unix timestamp of session start
    uint32_t duration_seconds;          // Total session duration
    float total_distance_meters;        // Total distance rowed
    float average_pace_sec_500m;        // Average pace (seconds per 500m)
    float average_power_watts;          // Average power output
    uint32_t stroke_count;              // Total strokes
    uint32_t total_calories;            // Total calories burned
    float drag_factor;                  // Drag factor used
} session_record_t;

// ============================================================================
// Function Declarations
// ============================================================================

/**
 * Initialize physics engine with configuration
 * @param metrics Pointer to metrics structure to initialize
 * @param config Pointer to configuration
 */
void rowing_physics_init(rowing_metrics_t *metrics, const config_t *config);

/**
 * Process a new flywheel pulse event
 * @param metrics Pointer to metrics structure
 * @param pulse_time_us Timestamp of the pulse
 */
void rowing_physics_process_flywheel_pulse(rowing_metrics_t *metrics, int64_t pulse_time_us);

/**
 * Auto-calibrate drag coefficient during recovery phases
 * @param metrics Pointer to metrics structure
 * @param omega Current angular velocity (rad/s)
 * @param alpha Current angular acceleration (rad/s²)
 */
void rowing_physics_calibrate_drag(rowing_metrics_t *metrics, float omega, float alpha);

/**
 * Calculate instantaneous power output
 * @param metrics Pointer to metrics structure
 */
void rowing_physics_calculate_power(rowing_metrics_t *metrics);

/**
 * Calculate distance for completed stroke
 * @param metrics Pointer to metrics structure
 * @param calibration_factor Distance calibration factor
 */
void rowing_physics_calculate_distance(rowing_metrics_t *metrics, float calibration_factor);

/**
 * Calculate pace (time per 500m)
 * @param metrics Pointer to metrics structure
 */
void rowing_physics_calculate_pace(rowing_metrics_t *metrics);

/**
 * Calculate calories burned
 * @param metrics Pointer to metrics structure
 * @param user_weight_kg User weight in kilograms
 */
void rowing_physics_calculate_calories(rowing_metrics_t *metrics, float user_weight_kg);

/**
 * Format pace as MM:SS.s string
 * @param pace_seconds Pace in seconds
 * @param buffer Output buffer
 * @param buf_len Buffer length
 */
void rowing_physics_format_pace(float pace_seconds, char *buffer, size_t buf_len);

/**
 * Reset metrics for new session
 * @param metrics Pointer to metrics structure
 */
void rowing_physics_reset(rowing_metrics_t *metrics);

/**
 * Update elapsed time
 * @param metrics Pointer to metrics structure
 */
void rowing_physics_update_elapsed_time(rowing_metrics_t *metrics);

#endif // ROWING_PHYSICS_H
