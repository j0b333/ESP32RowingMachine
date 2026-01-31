/**
 * @file session_manager.c
 * @brief Session tracking and history management
 */

#include "session_manager.h"
#include "app_config.h"
#include "web_server.h"
#include "wifi_manager.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <time.h>

static const char *TAG = "SESSION";

// NVS namespace for sessions
#define SESSION_NVS_NAMESPACE   "sessions"

// Maximum number of sessions to store
#define MAX_STORED_SESSIONS     20

// Sample buffer size - allocate in PSRAM if available
// 7200 samples = 2 hours at 1 sample/sec, 8 bytes each = 57.6KB
#define SAMPLE_BUFFER_SIZE      7200

// Current session state
static uint32_t s_current_session_id = 0;
static int64_t s_session_start_time = 0;         // esp_timer value for elapsed time calculation
static int64_t s_session_start_unix_ms = 0;      // Unix epoch timestamp in milliseconds
static uint32_t s_session_count = 0;
static uint32_t s_stroke_count_at_resume = 0;    // Stroke count when session started/resumed (for auto-pause)

// Sample buffer for current session
static sample_data_t *s_sample_buffer = NULL;
static uint32_t s_sample_count = 0;
static float s_last_distance = 0;
static uint32_t s_heart_rate_sum = 0;
static uint32_t s_heart_rate_count = 0;
static uint8_t s_max_heart_rate = 0;
static float s_stroke_rate_sum = 0;
static uint32_t s_stroke_rate_samples = 0;

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
    
    // Allocate sample buffer (try PSRAM first, fallback to regular heap)
#ifdef CONFIG_SPIRAM
    s_sample_buffer = heap_caps_malloc(SAMPLE_BUFFER_SIZE * sizeof(sample_data_t), MALLOC_CAP_SPIRAM);
    if (s_sample_buffer) {
        ESP_LOGI(TAG, "Sample buffer allocated in PSRAM (%u bytes)", 
                 SAMPLE_BUFFER_SIZE * sizeof(sample_data_t));
    }
#endif
    if (s_sample_buffer == NULL) {
        s_sample_buffer = malloc(SAMPLE_BUFFER_SIZE * sizeof(sample_data_t));
        if (s_sample_buffer) {
            ESP_LOGI(TAG, "Sample buffer allocated in heap (%u bytes)", 
                     (unsigned int)(SAMPLE_BUFFER_SIZE * sizeof(sample_data_t)));
        } else {
            ESP_LOGE(TAG, "Failed to allocate sample buffer!");
        }
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
    
    // Capture Unix epoch time in milliseconds for companion app compatibility
    // If SNTP time is not synced, this will be 0 and will be populated later or default to uptime
    s_session_start_unix_ms = wifi_manager_get_unix_time_ms();
    if (s_session_start_unix_ms > 0) {
        ESP_LOGI(TAG, "Session start time: Unix epoch %lld ms", (long long)s_session_start_unix_ms);
    } else {
        ESP_LOGW(TAG, "SNTP time not synced, session will use uptime-based timestamp");
    }
    
    // Reset sample buffer for new session
    s_sample_count = 0;
    s_last_distance = 0;
    s_heart_rate_sum = 0;
    s_heart_rate_count = 0;
    s_max_heart_rate = 0;
    s_stroke_rate_sum = 0;
    s_stroke_rate_samples = 0;
    
    // Track stroke count at session start for auto-pause logic
    s_stroke_count_at_resume = metrics->stroke_count;
    
    // Set session start time and un-pause
    metrics->session_start_time_us = s_session_start_time;
    metrics->is_paused = false;
    metrics->pause_start_time_us = 0;
    metrics->total_paused_time_ms = 0;
    metrics->last_resume_time_us = s_session_start_time;  // Track when session started/resumed
    
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
    // Use Unix epoch milliseconds if available, otherwise fall back to esp_timer value
    // The companion app expects Unix epoch milliseconds for startTime
    if (s_session_start_unix_ms > 0) {
        record.start_timestamp = s_session_start_unix_ms;
        ESP_LOGI(TAG, "Saving session with Unix timestamp: %lld ms", (long long)s_session_start_unix_ms);
    } else {
        // Fallback: use microseconds since boot / 1000 to get ms (for legacy compatibility)
        // This will result in a small timestamp that the app can detect as invalid
        record.start_timestamp = s_session_start_time / 1000;
        ESP_LOGW(TAG, "Saving session with uptime-based timestamp (SNTP not synced)");
    }
    record.duration_seconds = metrics->elapsed_time_ms / 1000;
    record.total_distance_meters = metrics->total_distance_meters;
    record.average_pace_sec_500m = metrics->average_pace_sec_500m;
    record.average_power_watts = metrics->average_power_watts;
    record.stroke_count = metrics->stroke_count;
    record.total_calories = metrics->total_calories;
    record.drag_factor = metrics->drag_factor;
    record.sample_count = s_sample_count;
    record.max_heart_rate = s_max_heart_rate;
    record.synced = 0;  // Not synced initially
    
    // Calculate average heart rate from samples
    if (s_heart_rate_count > 0) {
        record.average_heart_rate = (float)s_heart_rate_sum / (float)s_heart_rate_count;
    } else {
        record.average_heart_rate = 0;
    }
    
    // Calculate average stroke rate from samples
    if (s_stroke_rate_samples > 0) {
        record.average_stroke_rate = s_stroke_rate_sum / (float)s_stroke_rate_samples;
    } else {
        record.average_stroke_rate = metrics->avg_stroke_rate_spm;
    }
    
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
    
    // Save sample data if we have samples
    if (s_sample_count > 0 && s_sample_buffer != NULL) {
        char samples_key[16];
        snprintf(samples_key, sizeof(samples_key), "d%lu", (unsigned long)(s_current_session_id % MAX_STORED_SESSIONS));
        
        // Save all samples - NVS will return error if storage is full
        // With 8 bytes per sample, a 2-hour session is ~57KB
        ret = nvs_set_blob(handle, samples_key, s_sample_buffer, s_sample_count * sizeof(sample_data_t));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save samples: %s", esp_err_to_name(ret));
            // Continue anyway - session record is saved
        } else {
            ESP_LOGI(TAG, "Saved %lu samples for session (%lu bytes)", 
                     (unsigned long)s_sample_count,
                     (unsigned long)(s_sample_count * sizeof(sample_data_t)));
        }
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
    
    // Reset session_start_time to stop timer from counting
    metrics->session_start_time_us = 0;
    metrics->elapsed_time_ms = 0;
    
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

/**
 * Delete a specific session from history
 */
esp_err_t session_manager_delete_session(uint32_t session_id) {
    if (session_id == 0 || session_id > s_session_count) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // First verify the session exists
    session_record_t record;
    esp_err_t ret = session_manager_get_session(session_id, &record);
    if (ret != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    
    nvs_handle_t handle;
    ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for delete: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create key for this session
    char key[16];
    snprintf(key, sizeof(key), "s%lu", (unsigned long)(session_id % MAX_STORED_SESSIONS));
    
    // Erase the session entry
    ret = nvs_erase_key(handle, key);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase session: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }
    
    // Also erase sample data
    char samples_key[16];
    snprintf(samples_key, sizeof(samples_key), "d%lu", (unsigned long)(session_id % MAX_STORED_SESSIONS));
    nvs_erase_key(handle, samples_key);  // Ignore error if not found
    
    nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Session #%lu deleted", (unsigned long)session_id);
    
    return ESP_OK;
}

/**
 * Mark a session as synced
 */
esp_err_t session_manager_set_synced(uint32_t session_id) {
    if (session_id == 0 || session_id > s_session_count) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // First get the session
    session_record_t record;
    esp_err_t ret = session_manager_get_session(session_id, &record);
    if (ret != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Update synced flag
    record.synced = 1;
    
    // Save back to NVS
    nvs_handle_t handle;
    ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for sync update: %s", esp_err_to_name(ret));
        return ret;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "s%lu", (unsigned long)(session_id % MAX_STORED_SESSIONS));
    
    ret = nvs_set_blob(handle, key, &record, sizeof(record));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update session sync status: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }
    
    nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Session #%lu marked as synced", (unsigned long)session_id);
    
    return ESP_OK;
}

/**
 * Delete all sessions that have been synced
 * Iterates through all possible session slots since sessions use modulo storage
 */
esp_err_t session_manager_delete_synced(void) {
    uint32_t deleted_count = 0;
    
    // Iterate through all possible session slots
    // Sessions are stored at slot = session_id % MAX_STORED_SESSIONS
    // So we need to check each slot, not just session IDs up to count
    for (uint32_t slot = 0; slot < MAX_STORED_SESSIONS; slot++) {
        nvs_handle_t handle;
        esp_err_t ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READONLY, &handle);
        if (ret != ESP_OK) {
            continue;
        }
        
        char key[16];
        snprintf(key, sizeof(key), "s%lu", (unsigned long)slot);
        
        session_record_t record;
        size_t len = sizeof(session_record_t);
        ret = nvs_get_blob(handle, key, &record, &len);
        nvs_close(handle);
        
        if (ret == ESP_OK && record.synced) {
            if (session_manager_delete_session(record.session_id) == ESP_OK) {
                deleted_count++;
            }
        }
    }
    
    ESP_LOGI(TAG, "Deleted %lu synced sessions", (unsigned long)deleted_count);
    
    return ESP_OK;
}

/**
 * Record a per-second sample during active workout
 * Stores velocity (m/s) instead of pace for Health Connect compatibility
 */
esp_err_t session_manager_record_sample(rowing_metrics_t *metrics, uint8_t heart_rate) {
    if (s_current_session_id == 0 || s_sample_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_sample_count >= SAMPLE_BUFFER_SIZE) {
        // Buffer full - could implement circular buffer or stop recording
        return ESP_ERR_NO_MEM;
    }
    
    sample_data_t *sample = &s_sample_buffer[s_sample_count];
    
    // Convert values to packed format with proper clamping before cast
    float power = metrics->instantaneous_power_watts;
    if (power < 0) power = 0;
    if (power > 65535) power = 65535;
    sample->power_watts = (uint16_t)power;
    
    // Store velocity in cm/s (convert from pace sec/500m)
    // velocity (m/s) = 500 / pace (sec/500m)
    // velocity (cm/s) = 50000 / pace (sec/500m)
    float pace = metrics->instantaneous_pace_sec_500m;
    float velocity_cm_s = 0;
    if (pace > 0 && pace < 9999) {
        velocity_cm_s = 50000.0f / pace;
    }
    if (velocity_cm_s > 65535) velocity_cm_s = 65535;
    sample->velocity_cm_s = (uint16_t)velocity_cm_s;
    
    sample->heart_rate = heart_rate;
    sample->reserved = 0;  // Reserved field (was stroke_rate)
    
    // Calculate distance delta since last sample
    float distance_delta = metrics->total_distance_meters - s_last_distance;
    if (distance_delta < 0) distance_delta = 0;  // Handle reset
    s_last_distance = metrics->total_distance_meters;
    float distance_dm = distance_delta * 10.0f;
    if (distance_dm > 65535) distance_dm = 65535;
    sample->distance_dm = (uint16_t)distance_dm;
    
    s_sample_count++;
    
    // Accumulate for averages and track max
    if (heart_rate > 0) {
        s_heart_rate_sum += heart_rate;
        s_heart_rate_count++;
        if (heart_rate > s_max_heart_rate) {
            s_max_heart_rate = heart_rate;
        }
    }
    if (metrics->stroke_rate_spm > 0) {
        s_stroke_rate_sum += metrics->stroke_rate_spm;
        s_stroke_rate_samples++;
    }
    
    return ESP_OK;
}

/**
 * Get sample data for a session
 */
esp_err_t session_manager_get_samples(uint32_t session_id, sample_data_t *buffer, 
                                       uint32_t buffer_size, uint32_t *sample_count) {
    if (buffer == NULL || sample_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *sample_count = 0;
    
    // If requesting current session, return from buffer
    if (session_id == s_current_session_id && s_sample_buffer != NULL) {
        uint32_t count = s_sample_count < buffer_size ? s_sample_count : buffer_size;
        memcpy(buffer, s_sample_buffer, count * sizeof(sample_data_t));
        *sample_count = count;
        return ESP_OK;
    }
    
    // Load from NVS
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(SESSION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char samples_key[16];
    snprintf(samples_key, sizeof(samples_key), "d%lu", (unsigned long)(session_id % MAX_STORED_SESSIONS));
    
    size_t len = buffer_size * sizeof(sample_data_t);
    ret = nvs_get_blob(handle, samples_key, buffer, &len);
    
    nvs_close(handle);
    
    if (ret == ESP_OK) {
        *sample_count = len / sizeof(sample_data_t);
    }
    
    return ret;
}

/**
 * Get sample count for current session
 */
uint32_t session_manager_get_current_sample_count(void) {
    return s_sample_count;
}

/**
 * Handle auto-start and auto-pause based on flywheel activity
 * Call this periodically from the metrics update task
 * 
 * Robustness improvements:
 * - Require at least one completed stroke before auto-starting (not just flywheel pulses)
 * - Track stroke count at session start/resume to detect genuine activity
 * - Auto-pause if no new strokes have occurred since resume
 */
esp_err_t session_manager_check_activity(rowing_metrics_t *metrics, const config_t *config) {
    if (metrics == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Skip auto-start/pause during inertia calibration
    // Calibration requires flywheel activity but should not start a session
    if (web_server_is_calibrating_inertia()) {
        return ESP_OK;
    }
    
    // If auto-pause is disabled (0 seconds), don't do anything
    if (config->auto_pause_seconds == 0) {
        return ESP_OK;
    }
    
    int64_t now = esp_timer_get_time();
    int32_t auto_pause_timeout_ms = (int32_t)config->auto_pause_seconds * 1000;
    
    // Use last stroke start time for detecting activity
    // This requires an actual drive phase, not just random flywheel movement
    int64_t last_activity_time = metrics->last_stroke_start_time_us;
    
    // Check if there's recent rowing activity (drive phase detected recently)
    // If last_activity_time is 0, there's never been activity
    bool has_recent_activity = false;
    int64_t time_since_activity_ms = 0;
    if (last_activity_time > 0) {
        time_since_activity_ms = (now - last_activity_time) / 1000;
        has_recent_activity = (time_since_activity_ms < auto_pause_timeout_ms);
    }
    
    // For auto-start, require at least one completed stroke to avoid spurious starts
    // A completed stroke means stroke_count > 0, which requires going through drive and recovery phases
    bool has_completed_stroke = (metrics->stroke_count > 0);
    
    // Current state
    bool session_active = (s_current_session_id > 0);
    bool is_paused = metrics->is_paused;
    
    if (has_recent_activity && has_completed_stroke) {
        // Genuine rowing activity detected (completed stroke + recent activity)
        if (!session_active) {
            // No session yet - auto-start a new session
            ESP_LOGI(TAG, "Auto-starting session (stroke #%lu detected)", (unsigned long)metrics->stroke_count);
            session_manager_start_session(metrics);
            s_stroke_count_at_resume = metrics->stroke_count;  // Track stroke count at start
            metrics->is_paused = false;
        } else if (is_paused) {
            // Session exists but is paused - auto-resume
            ESP_LOGI(TAG, "Auto-resuming session (stroke #%lu detected)", (unsigned long)metrics->stroke_count);
            
            // Calculate pause duration and add to total
            if (metrics->pause_start_time_us > 0) {
                int64_t paused_duration_us = now - metrics->pause_start_time_us;
                if (paused_duration_us > 0) {
                    metrics->total_paused_time_ms += (uint32_t)(paused_duration_us / 1000);
                }
            }
            
            // If session_start_time was 0 (reset state), set it now
            if (metrics->session_start_time_us == 0) {
                metrics->session_start_time_us = now;
            }
            
            s_stroke_count_at_resume = metrics->stroke_count;  // Track stroke count at resume
            metrics->is_paused = false;
            metrics->pause_start_time_us = 0;
            metrics->last_resume_time_us = now;
        }
    } else if (!has_recent_activity) {
        // No recent activity (timeout exceeded)
        // Auto-pause if: session active, not already paused, and strokes occurred since last resume
        bool had_strokes_since_resume = (metrics->stroke_count > s_stroke_count_at_resume);
        if (session_active && !is_paused && had_strokes_since_resume) {
            // Session is running but no activity - auto-pause
            ESP_LOGI(TAG, "Auto-pausing session (idle for %lld ms, strokes: %lu->%lu)", 
                     (long long)time_since_activity_ms,
                     (unsigned long)s_stroke_count_at_resume,
                     (unsigned long)metrics->stroke_count);
            metrics->is_paused = true;
            metrics->pause_start_time_us = now;
        } else if (session_active && !is_paused && !had_strokes_since_resume) {
            // Session started but no strokes completed since resume - this was likely a spurious start
            // Auto-pause immediately to stop the timer
            ESP_LOGI(TAG, "Auto-pausing session (no completed strokes since start/resume)");
            metrics->is_paused = true;
            metrics->pause_start_time_us = now;
        }
    }
    
    return ESP_OK;
}
