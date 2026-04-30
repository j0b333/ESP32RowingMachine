/**
 * @file display.h
 * @brief Display HAL — uniform API across mono OLEDs and color TFTs.
 *
 * The application calls high-level "render" functions which the active
 * driver translates into raw pixel commands. When the display is
 * disabled at compile time, all functions are cheap no-ops so the
 * application code does not need any `#if` guards.
 *
 * To avoid coupling this component to application data structures, the
 * caller passes a `display_metrics_t` view struct which it has populated
 * from its own internal state.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hw_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Snapshot of metrics for rendering. Populated by the application from
 *  its `rowing_metrics_t` to keep this HAL decoupled. */
typedef struct {
    float    instantaneous_pace_sec_500m;
    float    average_pace_sec_500m;
    float    display_power_watts;
    float    average_power_watts;
    float    stroke_rate_spm;
    float    total_distance_meters;
    float    drag_factor;
    float    moment_of_inertia;
    uint32_t elapsed_time_ms;
    uint32_t stroke_count;
    uint32_t total_calories;
    uint32_t drag_calibration_samples;
    uint8_t  heart_rate_bpm;
    uint8_t  current_phase;     /* 0=idle 1=drive 2=recovery */
    bool     is_active;
    bool     is_paused;
    bool     calibration_complete;
} display_metrics_t;

/** Initialize the display driver selected at compile time. Returns
 *  ESP_OK when no display is configured. */
esp_err_t display_init(void);

/** Release driver resources. */
void display_deinit(void);

/** True if a display is enabled and successfully initialized. */
bool display_is_ready(void);

/** Set backlight brightness (0..100%). No-op when not supported. */
esp_err_t display_set_backlight(uint8_t pct);

/** Get current backlight (0..100). */
uint8_t display_get_backlight(void);

/** Display geometry as configured. */
uint16_t display_width(void);
uint16_t display_height(void);

/* High-level rendering --------------------------------------------------- */

void display_render_splash(const char *version);
void display_render_status(const char *l1, const char *l2,
                           const char *l3, const char *l4);
void display_render_metrics(const display_metrics_t *m);

/* Screen navigation ------------------------------------------------------ */

void    display_next_screen(void);
void    display_prev_screen(void);
void    display_set_screen(uint8_t idx);
uint8_t display_get_screen(void);
uint8_t display_get_screen_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
