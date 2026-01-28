/**
 * @file metrics_calculator.h
 * @brief High-level metrics calculation and aggregation
 */

#ifndef METRICS_CALCULATOR_H
#define METRICS_CALCULATOR_H

#include "rowing_physics.h"

/**
 * Initialize metrics calculator
 * @param metrics Pointer to metrics structure
 * @param config Pointer to configuration
 */
void metrics_calculator_init(rowing_metrics_t *metrics, const config_t *config);

/**
 * Update all derived metrics
 * Called periodically from main task
 * @param metrics Pointer to metrics structure
 * @param config Pointer to configuration
 */
void metrics_calculator_update(rowing_metrics_t *metrics, const config_t *config);

/**
 * Get metrics as a snapshot (thread-safe copy)
 * @param metrics Source metrics
 * @param snapshot Destination snapshot
 */
void metrics_calculator_get_snapshot(const rowing_metrics_t *metrics, rowing_metrics_t *snapshot);

/**
 * Format metrics as JSON string
 * @param metrics Pointer to metrics structure
 * @param buffer Output buffer
 * @param buf_len Buffer length
 * @return Number of characters written
 */
int metrics_calculator_to_json(const rowing_metrics_t *metrics, char *buffer, size_t buf_len);

/**
 * Reset all metrics for new session
 * @param metrics Pointer to metrics structure
 */
void metrics_calculator_reset(rowing_metrics_t *metrics);

#endif // METRICS_CALCULATOR_H
