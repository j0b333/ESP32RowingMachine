/**
 * @file stroke_detector.c
 * @brief Stroke phase detection using flywheel velocity patterns and seat sensor
 * 
 * Implements stroke detection algorithm based on:
 * - Angular velocity thresholds
 * - Acceleration patterns
 * - Seat sensor triggers
 */

#include "stroke_detector.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "STROKE";

// Configurable thresholds (can be updated from config)
static float g_drive_start_velocity = DRIVE_START_VELOCITY_THRESHOLD;
static float g_drive_accel_threshold = DRIVE_ACCELERATION_THRESHOLD;
static float g_recovery_velocity = RECOVERY_VELOCITY_THRESHOLD;
static float g_distance_calibration = DEFAULT_DISTANCE_PER_REV;

/**
 * Initialize stroke detector with configuration
 */
void stroke_detector_init(const config_t *config) {
    if (config != NULL) {
        g_drive_start_velocity = config->drive_start_threshold_rad_s;
        g_drive_accel_threshold = config->drive_accel_threshold_rad_s2;
        g_recovery_velocity = config->recovery_threshold_rad_s;
        g_distance_calibration = config->distance_calibration_factor;
    }
    
    ESP_LOGI(TAG, "Stroke detector initialized");
    ESP_LOGI(TAG, "Drive start threshold: %.1f rad/s", g_drive_start_velocity);
    ESP_LOGI(TAG, "Drive accel threshold: %.1f rad/s²", g_drive_accel_threshold);
    ESP_LOGI(TAG, "Recovery threshold: %.1f rad/s", g_recovery_velocity);
}

/**
 * Update stroke phase detection
 * Called when new flywheel data is available
 */
void stroke_detector_update(rowing_metrics_t *metrics) {
    float omega = metrics->angular_velocity_rad_s;
    float alpha = metrics->angular_acceleration_rad_s2;
    stroke_phase_t current_phase = metrics->current_phase;
    int64_t now = esp_timer_get_time();
    
    switch (current_phase) {
        case STROKE_PHASE_IDLE:
            // Check for drive start conditions
            if (omega > g_drive_start_velocity && alpha > g_drive_accel_threshold) {
                // Transition to drive phase
                metrics->current_phase = STROKE_PHASE_DRIVE;
                metrics->last_stroke_start_time_us = now;
                metrics->peak_velocity_in_stroke = omega;
                metrics->drive_phase_work_joules = 0;
                metrics->display_power_watts = 0;  // Reset display power for new stroke
                ESP_LOGD(TAG, "Drive phase started (ω=%.1f, α=%.1f)", omega, alpha);
            }
            break;
            
        case STROKE_PHASE_DRIVE:
            // Track peak velocity
            if (omega > metrics->peak_velocity_in_stroke) {
                metrics->peak_velocity_in_stroke = omega;
            }
            
            // Check for recovery transition
            // Recovery starts when velocity peaks and starts decreasing
            if (alpha < 0 && omega < metrics->peak_velocity_in_stroke * 0.9f) {
                // Velocity peaked and now decreasing → end of drive
                metrics->current_phase = STROKE_PHASE_RECOVERY;
                metrics->last_stroke_end_time_us = now;
                
                uint32_t drive_duration_ms = (uint32_t)((now - metrics->last_stroke_start_time_us) / 1000);
                metrics->drive_phase_duration_ms = drive_duration_ms;
                
                // Increment stroke count if duration is valid
                if (drive_duration_ms >= MINIMUM_STROKE_DURATION_MS) {
                    metrics->stroke_count++;
                    
                    // Calculate stroke rate
                    stroke_detector_calculate_stroke_rate(metrics);
                    
                    // Calculate distance for this stroke
                    rowing_physics_calculate_distance(metrics, g_distance_calibration);
                    
                    // At end of drive, set display power to the peak achieved
                    // This holds the power display steady during recovery
                    metrics->display_power_watts = metrics->display_power_watts;  // Already set during drive
                    
                    ESP_LOGI(TAG, "Stroke #%lu complete, SPM=%.1f, dist=%.1fm, power=%.0fW", 
                             (unsigned long)metrics->stroke_count, 
                             metrics->stroke_rate_spm,
                             metrics->total_distance_meters,
                             metrics->display_power_watts);
                } else {
                    ESP_LOGD(TAG, "Drive too short (%lums), not counting stroke", 
                             (unsigned long)drive_duration_ms);
                }
            }
            break;
            
        case STROKE_PHASE_RECOVERY:
            // Check for next drive or return to idle
            if (omega < g_recovery_velocity) {
                // Very slow, transition to idle
                metrics->current_phase = STROKE_PHASE_IDLE;
                metrics->peak_velocity_in_stroke = 0;
                
                // Calculate recovery duration
                uint32_t recovery_duration_ms = (uint32_t)((now - metrics->last_stroke_end_time_us) / 1000);
                metrics->recovery_phase_duration_ms = recovery_duration_ms;
                
                ESP_LOGD(TAG, "Transition to idle (ω=%.1f)", omega);
            } else if (alpha > g_drive_accel_threshold) {
                // Re-acceleration detected, new stroke starting
                metrics->current_phase = STROKE_PHASE_DRIVE;
                
                // Calculate recovery duration
                uint32_t recovery_duration_ms = (uint32_t)((now - metrics->last_stroke_end_time_us) / 1000);
                metrics->recovery_phase_duration_ms = recovery_duration_ms;
                
                // Start new stroke
                metrics->last_stroke_start_time_us = now;
                metrics->peak_velocity_in_stroke = omega;
                metrics->drive_phase_work_joules = 0;
                metrics->display_power_watts = 0;  // Reset display power for new stroke
                
                ESP_LOGD(TAG, "New drive phase started (ω=%.1f, α=%.1f)", omega, alpha);
            }
            break;
    }
}

/**
 * Process seat sensor trigger
 * The seat sensor triggers when the seat passes the mid-rail position
 */
void stroke_detector_process_seat_trigger(rowing_metrics_t *metrics) {
    stroke_phase_t current_phase = metrics->current_phase;
    int64_t now = esp_timer_get_time();
    
    // Seat trigger at mid-rail typically indicates drive phase
    // Use it to confirm or force drive phase detection
    if (current_phase == STROKE_PHASE_IDLE || current_phase == STROKE_PHASE_RECOVERY) {
        // Seat trigger confirms drive is happening
        // Check if velocity supports this
        if (metrics->angular_velocity_rad_s > g_recovery_velocity) {
            // Force transition to drive phase
            metrics->current_phase = STROKE_PHASE_DRIVE;
            
            if (current_phase == STROKE_PHASE_RECOVERY) {
                // Calculate recovery duration
                uint32_t recovery_duration_ms = (uint32_t)((now - metrics->last_stroke_end_time_us) / 1000);
                metrics->recovery_phase_duration_ms = recovery_duration_ms;
            }
            
            metrics->last_stroke_start_time_us = now;
            metrics->drive_phase_work_joules = 0;
            
            ESP_LOGD(TAG, "Drive phase confirmed by seat sensor");
        }
    }
    
    metrics->seat_trigger_count++;
}

/**
 * Calculate stroke rate (strokes per minute)
 */
void stroke_detector_calculate_stroke_rate(rowing_metrics_t *metrics) {
    if (metrics->stroke_count < 2) {
        metrics->stroke_rate_spm = 0;
        return;
    }
    
    // Calculate time for last complete stroke cycle (drive + recovery)
    uint32_t stroke_cycle_ms = metrics->drive_phase_duration_ms + metrics->recovery_phase_duration_ms;
    
    if (stroke_cycle_ms < 500) {
        // Too short, use previous value
        return;
    }
    
    // Convert to strokes per minute
    float instantaneous_spm = 60000.0f / (float)stroke_cycle_ms;
    
    // Clamp to reasonable range (10-60 SPM)
    if (instantaneous_spm < 10.0f) instantaneous_spm = 10.0f;
    if (instantaneous_spm > 60.0f) instantaneous_spm = 60.0f;
    
    // Apply exponential moving average filter for smoothing
    if (metrics->stroke_rate_spm == 0) {
        metrics->stroke_rate_spm = instantaneous_spm;
    } else {
        metrics->stroke_rate_spm = 0.7f * metrics->stroke_rate_spm + 0.3f * instantaneous_spm;
    }
    
    // Calculate average stroke rate for entire session
    int64_t elapsed_us = esp_timer_get_time() - metrics->session_start_time_us;
    float elapsed_min = (float)elapsed_us / 60000000.0f;
    if (elapsed_min > 0.1f) {
        metrics->avg_stroke_rate_spm = (float)metrics->stroke_count / elapsed_min;
    }
}

/**
 * Get current stroke phase as string
 */
const char* stroke_detector_phase_to_string(stroke_phase_t phase) {
    switch (phase) {
        case STROKE_PHASE_IDLE:     return "Idle";
        case STROKE_PHASE_DRIVE:    return "Drive";
        case STROKE_PHASE_RECOVERY: return "Recovery";
        default:                    return "Unknown";
    }
}
