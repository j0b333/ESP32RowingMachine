/**
 * @file utils.c
 * @brief Utility functions for the rowing monitor
 */

#include "utils.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "UTILS";

/**
 * Format time in seconds to HH:MM:SS string
 */
void utils_format_time(uint32_t total_seconds, char *buffer, size_t buf_len) {
    uint32_t hours = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;
    
    if (hours > 0) {
        snprintf(buffer, buf_len, "%lu:%02lu:%02lu", 
                 (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
    } else {
        snprintf(buffer, buf_len, "%lu:%02lu", 
                 (unsigned long)minutes, (unsigned long)seconds);
    }
}

/**
 * Format distance in meters to string with appropriate units
 */
void utils_format_distance(float meters, bool use_imperial, char *buffer, size_t buf_len) {
    if (use_imperial) {
        float yards = utils_meters_to_yards(meters);
        if (yards >= 1760) {
            snprintf(buffer, buf_len, "%.2f mi", yards / 1760.0f);
        } else {
            snprintf(buffer, buf_len, "%.0f yd", yards);
        }
    } else {
        if (meters >= 1000) {
            snprintf(buffer, buf_len, "%.2f km", meters / 1000.0f);
        } else {
            snprintf(buffer, buf_len, "%.0f m", meters);
        }
    }
}

/**
 * Convert meters to yards
 */
float utils_meters_to_yards(float meters) {
    return meters * 1.09361f;
}

/**
 * Convert kg to lbs
 */
float utils_kg_to_lbs(float kg) {
    return kg * 2.20462f;
}

/**
 * Clamp a float value between min and max
 */
float utils_clamp_f(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * Exponential moving average filter
 */
float utils_ema_filter(float current, float new_sample, float alpha) {
    return (1.0f - alpha) * current + alpha * new_sample;
}

/**
 * Get free heap memory
 */
uint32_t utils_get_free_heap(void) {
    return esp_get_free_heap_size();
}

/**
 * Get minimum free heap since boot
 */
uint32_t utils_get_min_free_heap(void) {
    return esp_get_minimum_free_heap_size();
}

/**
 * Restart the device
 */
void utils_restart(void) {
    esp_restart();
}

/**
 * Get uptime in seconds
 */
uint32_t utils_get_uptime_seconds(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/**
 * Track if SNTP has synced
 */
static bool s_time_synced = false;

/**
 * SNTP sync notification callback
 */
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Time synchronized via SNTP");
    s_time_synced = true;
}

/**
 * Initialize SNTP for time synchronization
 */
void utils_init_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP...");
    
    // Set timezone to UTC (apps will handle local time conversion)
    setenv("TZ", "UTC0", 1);
    tzset();
    
    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    
    ESP_LOGI(TAG, "SNTP initialized, waiting for time sync...");
}

/**
 * Check if time has been synchronized via SNTP
 */
bool utils_time_is_synced(void) {
    return s_time_synced;
}

/**
 * Get current Unix timestamp in milliseconds
 */
int64_t utils_get_unix_time_ms(void) {
    if (!s_time_synced) {
        return 0;
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}
