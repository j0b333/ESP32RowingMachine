/**
 * @file hardware.h
 * @brief Glue between the rowing-monitor application and the hardware
 *        HAL components (display, touch, buttons, encoder, audio, LED).
 *
 * This is the *single* place the application includes for all optional
 * peripherals. Each call is a no-op when the corresponding component
 * is disabled at compile time, so application code never needs `#if`
 * guards.
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include "esp_err.h"
#include "rowing_physics.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize all enabled HAL components. Returns the first error
 *  encountered (initialization continues for the rest). Cheap no-op
 *  when nothing is enabled. */
esp_err_t hardware_init(rowing_metrics_t *metrics, config_t *cfg);

/** Push the latest metrics to the display (cheap no-op when disabled). */
void      hardware_render_metrics(const rowing_metrics_t *metrics);

/** Update status indicator LED based on current rowing/connectivity state. */
void      hardware_update_indicator(const rowing_metrics_t *metrics,
                                    bool wifi_connected,
                                    bool ble_advertising,
                                    bool hr_connected);

/** Audible feedback hooks called from the application. */
void      hardware_on_stroke(void);
void      hardware_on_interval(void);

/** Tear-down */
void      hardware_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HARDWARE_H */
