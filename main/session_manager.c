/**
 * @file session_manager.c
 * @brief Session tracking and history management
 */

#include "session_manager.h"
#include "app_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <time.h>

static const char *TAG = "SESSION";

// NVS namespace for sessions
#define SESSION_NVS_NAMESPACE   "sessions"

// Maximum number of sessions to store
#define MAX_STORED_SESSIONS     20

// Current session state
static uint32_t s_current_session_id = 0;
static int64_t s_session_start_time = 0;
static uint32_t s_session_count = 0;

/**
 * Initialize session manager
 */
esp_err_t session_manager_init(void) {
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        // Load session count
        nvs_get_u32(handle, "count", &s_session_count);
        nvs_close(handle);
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_session_count = 0;
    }
    
    ESP_LOGI(TAG, "Session manager initialized, %lu sessions in history", 
             (unsigned long)s_session_count);
    
    return ESP_OK;
}

/**
 * Start a new session
 */
esp_err_t session_manager_start_session(rowing_metrics_t *metrics) {
    s_current_session_id = s_session_count + 1;
    s_session_start_time = esp_timer_get_time();
    
    // Reset metrics for new session
    metrics->session_start_time_us = s_session_start_time;
    
    ESP_LOGI(TAG, "Session #%lu started", (unsigned long)s_current_session_id);
    
    return ESP_OK;
}

/**
 * End current session and save to history
 */
esp_err_t session_manager_end_session(rowing_metrics_t *metrics) {
    if (s_current_session_id == 0) {
        ESP_LOGW(TAG, "No active session to end");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Only save if meaningful activity occurred
    if (metrics->stroke_count < 5 || metrics->total_distance_meters < 10.0f) {
        ESP_LOGI(TAG, "Session too short, not saving");
        s_current_session_id = 0;
        return ESP_OK;
    }
    
    // Create session record
    session_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.session_id = s_current_session_id;
    record.start_timestamp = s_session_start_time;
    record.duration_seconds = metrics->elapsed_time_ms / 1000;
    record.total_distance_meters = metrics->total_distance_meters;
    record.average_pace_sec_500m = metrics->average_pace_sec_500m;
    record.average_power_watts = metrics->average_power_watts;
    record.stroke_count = metrics->stroke_count;
    record.total_calories = metrics->total_calories;
    record.drag_factor = metrics->drag_factor;
    
    // Save to NVS
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create key for this session
    char key[16];
    snprintf(key, sizeof(key), "s%lu", (unsigned long)(s_current_session_id % MAX_STORED_SESSIONS));
    
    // Save record
    ret = nvs_set_blob(handle, key, &record, sizeof(record));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save session: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }
    
    // Update session count
    s_session_count = s_current_session_id;
    nvs_set_u32(handle, "count", s_session_count);
    
    nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Session #%lu saved: %.1fm, %lu strokes, %lu cal",
             (unsigned long)s_current_session_id,
             record.total_distance_meters,
             (unsigned long)record.stroke_count,
             (unsigned long)record.total_calories);
    
    s_current_session_id = 0;
    
    return ESP_OK;
}

/**
 * Get session record by ID
 */
esp_err_t session_manager_get_session(uint32_t session_id, session_record_t *record) {
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "s%lu", (unsigned long)(session_id % MAX_STORED_SESSIONS));
    
    size_t len = sizeof(session_record_t);
    ret = nvs_get_blob(handle, key, record, &len);
    
    nvs_close(handle);
    
    if (ret == ESP_OK && record->session_id != session_id) {
        // Slot has been overwritten with a newer session
        return ESP_ERR_NOT_FOUND;
    }
    
    return ret;
}

/**
 * Get number of stored sessions
 */
uint32_t session_manager_get_session_count(void) {
    return s_session_count;
}

/**
 * Clear all session history
 */
esp_err_t session_manager_clear_history(void) {
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    
    s_session_count = 0;
    
    ESP_LOGI(TAG, "Session history cleared");
    
    return ESP_OK;
}

/**
 * Get current session ID
 */
uint32_t session_manager_get_current_session_id(void) {
    return s_current_session_id;
}
