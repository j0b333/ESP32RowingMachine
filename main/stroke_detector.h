/**
 * @file stroke_detector.h
 * @brief Stroke phase detection using flywheel velocity patterns and seat sensor
 */

#ifndef STROKE_DETECTOR_H
#define STROKE_DETECTOR_H

#include "rowing_physics.h"

/**
 * Initialize stroke detector with configuration
 * @param config Pointer to configuration
 */
void stroke_detector_init(const config_t *config);

/**
 * Update stroke phase detection
 * Called continuously from main rowing task
 * @param metrics Pointer to metrics structure
 */
void stroke_detector_update(rowing_metrics_t *metrics);

/**
 * Process seat sensor trigger
 * Called from sensor task when seat sensor activates
 * @param metrics Pointer to metrics structure
 */
void stroke_detector_process_seat_trigger(rowing_metrics_t *metrics);

/**
 * Calculate stroke rate (strokes per minute)
 * @param metrics Pointer to metrics structure
 */
void stroke_detector_calculate_stroke_rate(rowing_metrics_t *metrics);

/**
 * Get current stroke phase as string
 * @param phase Stroke phase enum
 * @return String representation
 */
const char* stroke_detector_phase_to_string(stroke_phase_t phase);

#endif // STROKE_DETECTOR_H
