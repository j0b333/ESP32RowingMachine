/**
 * @file utils.h
 * @brief Utility functions for the rowing monitor
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Format time in seconds to HH:MM:SS string
 * @param total_seconds Total seconds
 * @param buffer Output buffer
 * @param buf_len Buffer length
 */
void utils_format_time(uint32_t total_seconds, char *buffer, size_t buf_len);

/**
 * Format distance in meters to string with appropriate units
 * @param meters Distance in meters
 * @param use_imperial Use imperial units (yards/miles)
 * @param buffer Output buffer
 * @param buf_len Buffer length
 */
void utils_format_distance(float meters, bool use_imperial, char *buffer, size_t buf_len);

/**
 * Convert meters to yards
 * @param meters Distance in meters
 * @return Distance in yards
 */
float utils_meters_to_yards(float meters);

/**
 * Convert kg to lbs
 * @param kg Weight in kilograms
 * @return Weight in pounds
 */
float utils_kg_to_lbs(float kg);

/**
 * Clamp a float value between min and max
 * @param value Value to clamp
 * @param min Minimum value
 * @param max Maximum value
 * @return Clamped value
 */
float utils_clamp_f(float value, float min, float max);

/**
 * Exponential moving average filter
 * @param current Current filtered value
 * @param new_sample New sample
 * @param alpha Filter coefficient (0-1, higher = faster response)
 * @return Updated filtered value
 */
float utils_ema_filter(float current, float new_sample, float alpha);

/**
 * Get free heap memory
 * @return Free heap in bytes
 */
uint32_t utils_get_free_heap(void);

/**
 * Get minimum free heap since boot
 * @return Minimum free heap in bytes
 */
uint32_t utils_get_min_free_heap(void);

/**
 * Restart the device
 */
void utils_restart(void);

/**
 * Get uptime in seconds
 * @return Uptime in seconds
 */
uint32_t utils_get_uptime_seconds(void);

#endif // UTILS_H
