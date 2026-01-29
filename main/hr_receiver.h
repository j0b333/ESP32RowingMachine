/**
 * @file hr_receiver.h
 * @brief Heart rate receiver for HeartRateToWeb app compatibility
 * 
 * Receives heart rate data from Galaxy Watch via HTTP POST
 * and stores samples for session recording.
 */

#ifndef HR_RECEIVER_H
#define HR_RECEIVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Heart rate sample structure
 */
typedef struct {
    int64_t timestamp_ms;   // Unix timestamp in milliseconds
    uint8_t bpm;            // Heart rate 0-255
} hr_sample_t;

/**
 * Initialize heart rate receiver
 * @return ESP_OK on success
 */
esp_err_t hr_receiver_init(void);

/**
 * Deinitialize heart rate receiver
 */
void hr_receiver_deinit(void);

/**
 * Update heart rate value
 * Called when a new HR value is received via HTTP POST
 * @param bpm Heart rate in beats per minute
 * @return ESP_OK on success
 */
esp_err_t hr_receiver_update(uint8_t bpm);

/**
 * Get current heart rate
 * @return Current heart rate or 0 if stale/unavailable
 */
uint8_t hr_receiver_get_current(void);

/**
 * Check if current heart rate is valid (not stale)
 * @return true if heart rate was updated within timeout period
 */
bool hr_receiver_is_valid(void);

/**
 * Get timestamp of last heart rate update
 * @return Timestamp in milliseconds
 */
int64_t hr_receiver_get_last_update_time(void);

/**
 * Start recording HR samples (called when workout starts)
 */
void hr_receiver_start_recording(void);

/**
 * Stop recording HR samples (called when workout stops)
 */
void hr_receiver_stop_recording(void);

/**
 * Get recorded HR samples
 * @param samples Output array for samples
 * @param max_samples Maximum samples to return
 * @return Number of samples copied
 */
int hr_receiver_get_samples(hr_sample_t *samples, int max_samples);

/**
 * Get HR statistics from current recording
 * @param avg_hr Output: average heart rate
 * @param max_hr Output: maximum heart rate
 * @param sample_count Output: number of samples
 */
void hr_receiver_get_stats(uint8_t *avg_hr, uint8_t *max_hr, uint16_t *sample_count);

/**
 * Clear HR sample buffer
 */
void hr_receiver_clear_samples(void);

#endif // HR_RECEIVER_H
