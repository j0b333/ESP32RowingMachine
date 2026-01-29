/**
 * @file metrics_calculator.c
 * @brief High-level metrics calculation and aggregation
 */

#include "metrics_calculator.h"
#include "app_config.h"
#include "hr_receiver.h"
#include "ble_hr_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "METRICS";

static float g_user_weight_kg = DEFAULT_USER_WEIGHT_KG;

/**
 * Initialize metrics calculator
 */
void metrics_calculator_init(rowing_metrics_t *metrics, const config_t *config) {
    if (config != NULL) {
        g_user_weight_kg = config->user_weight_kg;
    }
    
    rowing_physics_init(metrics, config);
    
    ESP_LOGI(TAG, "Metrics calculator initialized");
    ESP_LOGI(TAG, "User weight: %.1f kg", g_user_weight_kg);
}

/**
 * Update all derived metrics
 */
void metrics_calculator_update(rowing_metrics_t *metrics, const config_t *config) {
    // Update elapsed time
    rowing_physics_update_elapsed_time(metrics);
    
    // Update calorie calculations
    rowing_physics_calculate_calories(metrics, g_user_weight_kg);
    
    // Update pace if we have distance
    if (metrics->total_distance_meters > 0) {
        rowing_physics_calculate_pace(metrics);
    }
}

/**
 * Get metrics as a thread-safe snapshot
 */
void metrics_calculator_get_snapshot(const rowing_metrics_t *metrics, rowing_metrics_t *snapshot) {
    // Simple copy - in a multi-threaded environment, this should use a mutex
    memcpy(snapshot, metrics, sizeof(rowing_metrics_t));
}

/**
 * Reset all metrics for new session
 */
void metrics_calculator_reset(rowing_metrics_t *metrics) {
    rowing_physics_reset(metrics);
    ESP_LOGI(TAG, "Metrics reset for new session");
}

/**
 * Format metrics as JSON string
 */
int metrics_calculator_to_json(const rowing_metrics_t *metrics, char *buffer, size_t buf_len) {
    char pace_str[16];
    char avg_pace_str[16];
    
    rowing_physics_format_pace(metrics->instantaneous_pace_sec_500m, pace_str, sizeof(pace_str));
    rowing_physics_format_pace(metrics->average_pace_sec_500m, avg_pace_str, sizeof(avg_pace_str));
    
    // Get heart rate info
    uint8_t heart_rate = hr_receiver_get_current();
    bool hr_valid = hr_receiver_is_valid();
    
    // Get HR statistics
    uint8_t avg_hr = 0, max_hr = 0;
    uint16_t hr_count = 0;
    hr_receiver_get_stats(&avg_hr, &max_hr, &hr_count);
    
    // Get BLE HR client state
    ble_hr_state_t hr_state = ble_hr_client_get_state();
    const char *hr_status;
    switch (hr_state) {
        case BLE_HR_STATE_CONNECTED:
            hr_status = "connected";
            break;
        case BLE_HR_STATE_SCANNING:
            hr_status = "scanning";
            break;
        case BLE_HR_STATE_CONNECTING:
            hr_status = "connecting";
            break;
        case BLE_HR_STATE_ERROR:
            hr_status = "error";
            break;
        default:
            hr_status = "idle";
            break;
    }
    
    return snprintf(buffer, buf_len,
        "{"
        "\"distance\":%.1f,"
        "\"pace\":\"%.1f\","
        "\"paceStr\":\"%s\","
        "\"avgPace\":%.1f,"
        "\"avgPaceStr\":\"%s\","
        "\"power\":%.0f,"
        "\"avgPower\":%.0f,"
        "\"peakPower\":%.0f,"
        "\"strokeRate\":%.1f,"
        "\"avgStrokeRate\":%.1f,"
        "\"strokeCount\":%lu,"
        "\"calories\":%lu,"
        "\"caloriesPerHour\":%.0f,"
        "\"elapsedTime\":%lu,"
        "\"dragFactor\":%.1f,"
        "\"isActive\":%s,"
        "\"isPaused\":%s,"
        "\"phase\":\"%s\","
        "\"heartRate\":%u,"
        "\"avgHeartRate\":%u,"
        "\"hrValid\":%s,"
        "\"hrStatus\":\"%s\""
        "}",
        metrics->total_distance_meters,
        metrics->instantaneous_pace_sec_500m,
        pace_str,
        metrics->average_pace_sec_500m,
        avg_pace_str,
        metrics->instantaneous_power_watts,
        metrics->average_power_watts,
        metrics->peak_power_watts,
        metrics->stroke_rate_spm,
        metrics->avg_stroke_rate_spm,
        (unsigned long)metrics->stroke_count,
        (unsigned long)metrics->total_calories,
        metrics->calories_per_hour,
        (unsigned long)(metrics->elapsed_time_ms / 1000),
        metrics->drag_factor,
        metrics->is_active ? "true" : "false",
        metrics->is_paused ? "true" : "false",
        metrics->current_phase == STROKE_PHASE_IDLE ? "idle" : 
            (metrics->current_phase == STROKE_PHASE_DRIVE ? "drive" : "recovery"),
        (unsigned int)heart_rate,
        (unsigned int)avg_hr,
        hr_valid ? "true" : "false",
        hr_status
    );
}
