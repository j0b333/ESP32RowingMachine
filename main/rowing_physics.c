/**
 * @file rowing_physics.c
 * @brief Core physics calculations based on OpenRowingMonitor algorithms
 * 
 * This module implements the physics-based rowing model including:
 * - Angular velocity and acceleration calculations
 * - Drag coefficient auto-calibration
 * - Power output calculation
 * - Distance and pace tracking
 * - Calorie estimation
 */

#include "rowing_physics.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>
#include <inttypes.h>

static const char *TAG = "PHYSICS";

/**
 * Initialize physics engine with default values
 */
void rowing_physics_init(rowing_metrics_t *metrics, const config_t *config) {
    memset(metrics, 0, sizeof(rowing_metrics_t));
    
    metrics->session_start_time_us = esp_timer_get_time();
    metrics->moment_of_inertia = config->moment_of_inertia;
    metrics->drag_coefficient = config->initial_drag_coefficient;
    metrics->current_phase = STROKE_PHASE_IDLE;
    metrics->best_pace_sec_500m = 999999.0f;  // Initialize to "infinite" pace
    metrics->valid_data = false;
    metrics->is_active = false;
    metrics->calibration_complete = false;
    
    ESP_LOGI(TAG, "Physics engine initialized");
    ESP_LOGI(TAG, "Moment of inertia: %.4f kg⋅m²", metrics->moment_of_inertia);
    ESP_LOGI(TAG, "Initial drag coefficient: %.6f", metrics->drag_coefficient);
}

/**
 * Reset metrics for a new session
 */
void rowing_physics_reset(rowing_metrics_t *metrics) {
    float moi = metrics->moment_of_inertia;
    float drag = metrics->drag_coefficient;
    bool cal_complete = metrics->calibration_complete;
    
    memset(metrics, 0, sizeof(rowing_metrics_t));
    
    // Preserve calibration data
    metrics->moment_of_inertia = moi;
    metrics->drag_coefficient = drag;
    metrics->calibration_complete = cal_complete;
    
    metrics->session_start_time_us = esp_timer_get_time();
    metrics->current_phase = STROKE_PHASE_IDLE;
    metrics->best_pace_sec_500m = 999999.0f;
    
    ESP_LOGI(TAG, "Session reset - metrics cleared");
}

/**
 * Update elapsed time
 */
void rowing_physics_update_elapsed_time(rowing_metrics_t *metrics) {
    // Don't update elapsed time if paused
    if (metrics->is_paused) {
        return;
    }
    
    int64_t now = esp_timer_get_time();
    // Subtract total paused time from elapsed
    uint32_t raw_elapsed_ms = (uint32_t)((now - metrics->session_start_time_us) / 1000);
    metrics->elapsed_time_ms = raw_elapsed_ms - metrics->total_paused_time_ms;
}

/**
 * Process new flywheel pulse
 * Called from sensor task when pulse detected
 */
void rowing_physics_process_flywheel_pulse(rowing_metrics_t *metrics, int64_t current_time_us) {
    int64_t previous_time_us = metrics->last_flywheel_time_us;
    
    // Update pulse count
    metrics->flywheel_pulse_count++;
    
    // Skip first pulse (no delta time yet)
    if (previous_time_us == 0) {
        metrics->last_flywheel_time_us = current_time_us;
        return;
    }
    
    // Calculate time delta (seconds)
    float delta_time_s = (float)(current_time_us - previous_time_us) / 1000000.0f;
    
    // Sanity check: ignore if delta time too short or too long
    if (delta_time_s < 0.001f || delta_time_s > 10.0f) {
        ESP_LOGW(TAG, "Invalid delta time: %.6f s", delta_time_s);
        metrics->last_flywheel_time_us = current_time_us;
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
    metrics->last_update_time_us = esp_timer_get_time();
    
    // Track peak velocity in current stroke
    if (angular_velocity > metrics->peak_velocity_in_stroke) {
        metrics->peak_velocity_in_stroke = angular_velocity;
    }
    
    // Mark data as valid after first successful calculation
    if (!metrics->valid_data && metrics->flywheel_pulse_count >= 2) {
        metrics->valid_data = true;
    }
    
    // Update drag calibration if in recovery phase
    if (metrics->current_phase == STROKE_PHASE_RECOVERY && 
        angular_acceleration < 0) {
        rowing_physics_calibrate_drag(metrics, angular_velocity, angular_acceleration);
    }
    
    // Calculate instantaneous power
    rowing_physics_calculate_power(metrics);
    
    // Log for debugging (only every N pulses to avoid spam)
    if (metrics->flywheel_pulse_count % DEBUG_LOG_EVERY_N_PULSES == 0) {
        ESP_LOGD(TAG, "ω=%.2f rad/s, α=%.2f rad/s², P=%.1f W", 
                 angular_velocity, angular_acceleration, 
                 metrics->instantaneous_power_watts);
    }
}

/**
 * Auto-calibrate drag coefficient during recovery phases
 * 
 * During recovery (when no power applied):
 *   τ_drag = I × α = -k × ω²
 *   Solving for k: k = -I × α / ω²
 */
void rowing_physics_calibrate_drag(rowing_metrics_t *metrics, float omega, float alpha) {
    // Avoid division by very small values
    if (fabsf(omega) < 1.0f) {
        return;
    }
    
    // Calculate instantaneous drag coefficient
    float measured_k = -metrics->moment_of_inertia * alpha / (omega * omega);
    
    // Sanity check - drag coefficient should be positive and reasonable
    if (measured_k < 0 || measured_k > 0.01f) {
        return;
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
    
    // Convert to Concept2-style drag factor (typically 100-200 range)
    // Drag factor = 1e6 * k (approximately)
    metrics->drag_factor = metrics->drag_coefficient * 1000000.0f;
    
    // Mark calibration complete after sufficient samples
    if (metrics->drag_calibration_samples >= 50 && !metrics->calibration_complete) {
        metrics->calibration_complete = true;
        ESP_LOGI(TAG, "Drag calibration complete: k=%.6f, DF=%.1f", 
                 metrics->drag_coefficient, metrics->drag_factor);
    }
}

/**
 * Calculate instantaneous power output
 * 
 * Power = (I × α + k × ω²) × ω
 * First term: power to accelerate flywheel
 * Second term: power to overcome drag
 */
void rowing_physics_calculate_power(rowing_metrics_t *metrics) {
    float omega = metrics->angular_velocity_rad_s;
    float alpha = metrics->angular_acceleration_rad_s2;
    float I = metrics->moment_of_inertia;
    float k = metrics->drag_coefficient;
    
    // Calculate power components
    float accel_power = I * alpha * omega;
    float drag_power = k * omega * omega * omega;
    float total_power = accel_power + drag_power;
    
    // Clamp to reasonable range (0 to 2000W)
    if (total_power < 0) total_power = 0;
    if (total_power > 2000) total_power = 2000;
    
    metrics->instantaneous_power_watts = total_power;
    
    // Update peak power
    if (total_power > metrics->peak_power_watts) {
        metrics->peak_power_watts = total_power;
    }
    
    // Accumulate work during drive phase
    if (metrics->current_phase == STROKE_PHASE_DRIVE && total_power > 0) {
        // Approximate time step (assume 50ms between updates for simplicity)
        float dt = 0.05f;
        metrics->drive_phase_work_joules += total_power * dt;
        metrics->total_work_joules += total_power * dt;
    }
    
    // Update average power (running average)
    if (metrics->current_phase == STROKE_PHASE_DRIVE && total_power > 10.0f) {
        uint32_t n = metrics->stroke_count;
        if (n == 0) n = 1;
        metrics->average_power_watts = (metrics->average_power_watts * (n - 1) + total_power) / n;
    }
}

/**
 * Calculate distance for completed stroke
 */
void rowing_physics_calculate_distance(rowing_metrics_t *metrics, float calibration_factor) {
    // Method 1: Use work-based distance calculation
    // Distance = Work / (resistance factor)
    // For simplicity, use calibration factor per stroke
    
    float distance_this_stroke = calibration_factor;
    
    // Alternative: Scale by power output
    if (metrics->average_power_watts > 10.0f) {
        // Adjust distance based on power relative to baseline (100W)
        float power_factor = sqrtf(metrics->average_power_watts / 100.0f);
        distance_this_stroke *= power_factor;
    }
    
    // Clamp to reasonable range (2-20 meters per stroke)
    if (distance_this_stroke < 2.0f) distance_this_stroke = 2.0f;
    if (distance_this_stroke > 20.0f) distance_this_stroke = 20.0f;
    
    metrics->total_distance_meters += distance_this_stroke;
    metrics->distance_per_stroke_meters = distance_this_stroke;
    
    // Reset drive phase work for next stroke
    metrics->drive_phase_work_joules = 0;
    
    // Update pace calculations
    rowing_physics_calculate_pace(metrics);
}

/**
 * Calculate pace (time per 500m)
 */
void rowing_physics_calculate_pace(rowing_metrics_t *metrics) {
    int64_t elapsed_us = esp_timer_get_time() - metrics->session_start_time_us;
    float elapsed_s = (float)elapsed_us / 1000000.0f;
    
    if (metrics->total_distance_meters < 1.0f) {
        metrics->instantaneous_pace_sec_500m = 999999.0f;
        metrics->average_pace_sec_500m = 999999.0f;
        return;
    }
    
    // Average pace for entire session: (time / distance) * 500
    metrics->average_pace_sec_500m = (elapsed_s / metrics->total_distance_meters) * 500.0f;
    
    // Instantaneous pace - use average for now
    // TODO: Implement rolling window for instantaneous pace
    metrics->instantaneous_pace_sec_500m = metrics->average_pace_sec_500m;
    
    // Update best pace
    if (metrics->instantaneous_pace_sec_500m < metrics->best_pace_sec_500m && 
        metrics->instantaneous_pace_sec_500m > 60.0f) {  // Sanity check: at least 1 min/500m
        metrics->best_pace_sec_500m = metrics->instantaneous_pace_sec_500m;
    }
}

/**
 * Calculate calories burned
 * Based on power output and time
 */
void rowing_physics_calculate_calories(rowing_metrics_t *metrics, float user_weight_kg) {
    // Rowing efficiency is approximately 20-25%
    // 1 watt = 0.01433 kcal/min (approximately)
    
    int64_t elapsed_us = esp_timer_get_time() - metrics->session_start_time_us;
    float elapsed_min = (float)elapsed_us / 60000000.0f;
    
    if (elapsed_min < 0.1f) {
        return;  // Too early to calculate
    }
    
    float avg_power = metrics->average_power_watts;
    float calories = avg_power * CALORIES_PER_WATT_MINUTE * elapsed_min;
    
    // Add baseline metabolic contribution (approximately 1 kcal/min)
    calories += elapsed_min * 1.0f;
    
    metrics->total_calories = (uint32_t)calories;
    
    // Calories per hour (current rate)
    metrics->calories_per_hour = calories * (60.0f / elapsed_min);
}

/**
 * Format pace as MM:SS.s string
 */
void rowing_physics_format_pace(float pace_seconds, char *buffer, size_t buf_len) {
    if (pace_seconds > 9999.0f || pace_seconds < 0) {
        snprintf(buffer, buf_len, "--:--.-");
        return;
    }
    
    uint32_t total_seconds = (uint32_t)pace_seconds;
    uint32_t minutes = total_seconds / 60;
    uint32_t seconds = total_seconds % 60;
    uint32_t tenths = (uint32_t)((pace_seconds - (float)total_seconds) * 10.0f);
    
    snprintf(buffer, buf_len, "%02" PRIu32 ":%02" PRIu32 ".%01" PRIu32, minutes, seconds, tenths);
}
