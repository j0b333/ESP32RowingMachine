/**
 * @file sensor_manager.h
 * @brief GPIO interrupt handling for flywheel and seat sensors
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "esp_err.h"
#include "rowing_physics.h"

/**
 * Initialize sensor GPIO and interrupts
 * @return ESP_OK on success
 */
esp_err_t sensor_manager_init(void);

/**
 * Deinitialize sensor manager
 */
void sensor_manager_deinit(void);

/**
 * Get current flywheel pulse count
 * @return Total pulse count
 */
uint32_t sensor_get_flywheel_count(void);

/**
 * Get timestamp of last flywheel pulse
 * @return Timestamp in microseconds
 */
int64_t sensor_get_last_flywheel_time(void);

/**
 * Get current seat trigger count
 * @return Total trigger count
 */
uint32_t sensor_get_seat_count(void);

/**
 * Get timestamp of last seat trigger
 * @return Timestamp in microseconds
 */
int64_t sensor_get_last_seat_time(void);

/**
 * Start sensor processing task
 * @param metrics Pointer to metrics structure for updates
 * @param config Pointer to configuration
 * @return ESP_OK on success
 */
esp_err_t sensor_manager_start_task(rowing_metrics_t *metrics, const config_t *config);

/**
 * Stop sensor processing task
 */
void sensor_manager_stop_task(void);

/**
 * Check if sensors are active (receiving pulses)
 * @return true if active
 */
bool sensor_manager_is_active(void);

/**
 * Reset sensor counters
 */
void sensor_manager_reset_counters(void);

#endif // SENSOR_MANAGER_H
