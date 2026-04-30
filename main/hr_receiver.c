/**
 * @file hr_receiver.c
 * @brief Heart rate receiver for HeartRateToWeb app compatibility
 */

#include "hr_receiver.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "HR_RECV";

// Heart rate stale timeout (5 seconds)
#define HR_STALE_TIMEOUT_MS     5000

// Maximum HR samples to store (2 hours at 1Hz)
#define MAX_HR_SAMPLES          7200

// Current heart rate state
static volatile uint8_t s_current_hr = 0;
static volatile int64_t s_last_update_time_ms = 0;

// HR sample buffer for recording
static hr_sample_t *s_hr_buffer = NULL;
static volatile int s_buffer_index = 0;
static volatile bool s_recording = false;

// Incremental running stats (maintained on each sample insert) so that
// hr_receiver_get_stats() is O(1). Previously this scanned the entire buffer
// on every call, and since the broadcast task calls it at 5 Hz, an hour-long
// session could spend a non-trivial fraction of CPU time inside the HR mutex.
static uint32_t s_hr_sum = 0;
static uint8_t  s_hr_max = 0;

// Mutex for thread safety
static SemaphoreHandle_t s_hr_mutex = NULL;

/**
 * Get current time in milliseconds
 */
static int64_t get_time_ms(void) {
    return esp_timer_get_time() / 1000;
}

/**
 * Initialize heart rate receiver
 */
esp_err_t hr_receiver_init(void) {
    // Create mutex
    if (s_hr_mutex == NULL) {
        s_hr_mutex = xSemaphoreCreateMutex();
        if (s_hr_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create HR mutex");
            return ESP_FAIL;
        }
    }
    
    // Allocate sample buffer (uses PSRAM if available)
    if (s_hr_buffer == NULL) {
        s_hr_buffer = heap_caps_malloc(MAX_HR_SAMPLES * sizeof(hr_sample_t), 
                                        MALLOC_CAP_DEFAULT);
        if (s_hr_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate HR buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    
    s_current_hr = 0;
    s_last_update_time_ms = 0;
    s_buffer_index = 0;
    s_recording = false;
    
    ESP_LOGI(TAG, "Heart rate receiver initialized");
    return ESP_OK;
}

/**
 * Deinitialize heart rate receiver
 */
void hr_receiver_deinit(void) {
    if (s_hr_buffer != NULL) {
        free(s_hr_buffer);
        s_hr_buffer = NULL;
    }
    
    if (s_hr_mutex != NULL) {
        vSemaphoreDelete(s_hr_mutex);
        s_hr_mutex = NULL;
    }
}

/**
 * Update heart rate value
 */
esp_err_t hr_receiver_update(uint8_t bpm) {
    if (bpm == 0 || bpm > 220) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int64_t now = get_time_ms();
    
    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    
    s_current_hr = bpm;
    s_last_update_time_ms = now;
    
    // Record sample if recording is active
    if (s_recording && s_buffer_index < MAX_HR_SAMPLES) {
        s_hr_buffer[s_buffer_index].timestamp_ms = now;
        s_hr_buffer[s_buffer_index].bpm = bpm;
        s_buffer_index++;
        // Maintain incremental stats so get_stats() is O(1)
        s_hr_sum += bpm;
        if (bpm > s_hr_max) s_hr_max = bpm;
    }
    
    xSemaphoreGive(s_hr_mutex);
    
    ESP_LOGD(TAG, "HR updated: %d bpm", bpm);
    
    return ESP_OK;
}

/**
 * Get current heart rate.
 * Returns 0 if no recent valid update has been received.
 *
 * Single mutex acquisition (was previously two — this function called
 * is_valid() which itself takes the mutex, doubling lock traffic on a hot
 * path called from the broadcast task at 5 Hz).
 */
uint8_t hr_receiver_get_current(void) {
    uint8_t hr;
    int64_t last_update;

    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    hr = s_current_hr;
    last_update = s_last_update_time_ms;
    xSemaphoreGive(s_hr_mutex);

    if (last_update == 0) {
        return 0;
    }

    int64_t now = get_time_ms();
    if ((now - last_update) >= HR_STALE_TIMEOUT_MS) {
        return 0;
    }

    return hr;
}

/**
 * Check if current heart rate is valid (not stale)
 */
bool hr_receiver_is_valid(void) {
    int64_t last_update;
    
    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    last_update = s_last_update_time_ms;
    xSemaphoreGive(s_hr_mutex);
    
    if (last_update == 0) {
        return false;
    }
    
    int64_t now = get_time_ms();
    return (now - last_update) < HR_STALE_TIMEOUT_MS;
}

/**
 * Get timestamp of last heart rate update
 */
int64_t hr_receiver_get_last_update_time(void) {
    return s_last_update_time_ms;
}

/**
 * Start recording HR samples
 */
void hr_receiver_start_recording(void) {
    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    s_buffer_index = 0;
    s_hr_sum = 0;
    s_hr_max = 0;
    s_recording = true;
    xSemaphoreGive(s_hr_mutex);
    
    ESP_LOGI(TAG, "HR recording started");
}

/**
 * Stop recording HR samples
 */
void hr_receiver_stop_recording(void) {
    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    s_recording = false;
    xSemaphoreGive(s_hr_mutex);
    
    ESP_LOGI(TAG, "HR recording stopped, %d samples collected", s_buffer_index);
}

/**
 * Get recorded HR samples
 * @param samples Output array for samples (must not be NULL)
 * @param max_samples Maximum samples to return (must be > 0)
 * @return Number of samples copied
 */
int hr_receiver_get_samples(hr_sample_t *samples, int max_samples) {
    if (samples == NULL || max_samples <= 0 || s_hr_buffer == NULL) {
        return 0;
    }
    
    int count = 0;
    
    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    count = (s_buffer_index < max_samples) ? s_buffer_index : max_samples;
    if (count > 0) {
        memcpy(samples, s_hr_buffer, count * sizeof(hr_sample_t));
    }
    xSemaphoreGive(s_hr_mutex);
    
    return count;
}

/**
 * Get HR statistics from current recording
 */
void hr_receiver_get_stats(uint8_t *avg_hr, uint8_t *max_hr, uint16_t *sample_count) {
    uint32_t sum;
    uint8_t max;
    int count;

    /* O(1): use the incrementally-maintained running sum/max instead of
     * scanning the entire 7200-element buffer on every call. The previous
     * implementation was the dominant CPU cost in long sessions. */
    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    count = s_buffer_index;
    sum = s_hr_sum;
    max = s_hr_max;
    xSemaphoreGive(s_hr_mutex);

    if (avg_hr) {
        *avg_hr = (count > 0) ? (uint8_t)(sum / (uint32_t)count) : 0;
    }
    if (max_hr) {
        *max_hr = max;
    }
    if (sample_count) {
        *sample_count = (uint16_t)count;
    }
}

/**
 * Clear HR sample buffer
 */
void hr_receiver_clear_samples(void) {
    xSemaphoreTake(s_hr_mutex, portMAX_DELAY);
    s_buffer_index = 0;
    s_hr_sum = 0;
    s_hr_max = 0;
    xSemaphoreGive(s_hr_mutex);
}
