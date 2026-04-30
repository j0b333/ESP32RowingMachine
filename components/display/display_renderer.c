/**
 * @file display_renderer.c
 * @brief High-level renderers built on top of driver primitives.
 *
 * Three layouts: 128x64 mono OLED, 240x small color, ≥320 large color.
 * Each layout exposes the same set of "screens" so the user can cycle
 * through them with a button or rotary encoder.
 */

#include <stdio.h>
#include <string.h>
#include "display.h"
#include "drivers/display_driver.h"
#include "sdkconfig.h"

#if CONFIG_ROWING_DISPLAY_ENABLED

#define SCREEN_COUNT 4

uint8_t display_renderer_screen_count(void) { return SCREEN_COUNT; }

/* Maximum displayable pace = 99:59 minutes per 500 m. Anything slower
 * is rendered as "--:--" to avoid overflowing the layout. */
#define MAX_DISPLAYABLE_PACE_SEC 5999.0f

static void format_pace(float seconds, char *out, size_t n)
{
    if (seconds <= 0.0f || seconds > MAX_DISPLAYABLE_PACE_SEC) {
        snprintf(out, n, "--:--");
        return;
    }
    int total = (int)(seconds + 0.5f);
    snprintf(out, n, "%d:%02d", total / 60, total % 60);
}

void display_renderer_draw_splash(const display_driver_t *d, const char *version)
{
    d->clear(HW_COLOR_BLACK);
    if (d->color) {
        d->draw_text(d->width / 2 - 60, d->height / 2 - 24,
                     "ROW MONITOR", 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        d->draw_text(4, d->height / 2 + 8, version, 1,
                     HW_COLOR_GRAY, HW_COLOR_BLACK);
    } else {
        d->draw_text(0, 0,  "ROW MONITOR", 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        d->draw_text(0, 24, version,        1, HW_COLOR_WHITE, HW_COLOR_BLACK);
        d->draw_text(0, 48, "starting...",  1, HW_COLOR_WHITE, HW_COLOR_BLACK);
    }
}

void display_renderer_draw_status(const display_driver_t *d,
                                  const char *l1, const char *l2,
                                  const char *l3, const char *l4)
{
    d->clear(HW_COLOR_BLACK);
    int line_h = d->color ? 18 : 12;
    int scale  = d->color ? 2  : 1;
    d->draw_text(2, line_h * 0 + 2, l1, scale, HW_COLOR_WHITE, HW_COLOR_BLACK);
    d->draw_text(2, line_h * 1 + 2, l2, scale, HW_COLOR_WHITE, HW_COLOR_BLACK);
    d->draw_text(2, line_h * 2 + 2, l3, scale, HW_COLOR_WHITE, HW_COLOR_BLACK);
    d->draw_text(2, line_h * 3 + 2, l4, scale, HW_COLOR_WHITE, HW_COLOR_BLACK);
}

void display_renderer_draw_metrics(const display_driver_t *d,
                                   const display_metrics_t *m,
                                   uint8_t screen_idx)
{
    char buf[24];
    char pace[12];

    d->clear(HW_COLOR_BLACK);

    /* MONO 128xN ----------------------------------------------------- */
    if (!d->color) {
        switch (screen_idx) {
        case 0:
            format_pace(m->instantaneous_pace_sec_500m, pace, sizeof pace);
            d->draw_text(0,  0, "PACE/500m", 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            d->draw_text(0, 12, pace,         3, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "%4.0fW",  m->display_power_watts);
            d->draw_text(72, 12, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "%4.0fm", m->total_distance_meters);
            d->draw_text(0,  48, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "%2.0fspm", m->stroke_rate_spm);
            d->draw_text(72, 48, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
            break;
        case 1:
            snprintf(buf, sizeof buf, "DIST %5.0fm", m->total_distance_meters);
            d->draw_text(0, 0,  buf, 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "STRK %5lu",
                     (unsigned long)m->stroke_count);
            d->draw_text(0, 16, buf, 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "PWR  %5.0fW", m->average_power_watts);
            d->draw_text(0, 32, buf, 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "CAL  %5lu",
                     (unsigned long)m->total_calories);
            d->draw_text(0, 48, buf, 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            break;
        case 2:
            d->draw_text(0, 0,  "DRAG", 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "DF %5.1f", m->drag_factor);
            d->draw_text(0, 12, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
            snprintf(buf, sizeof buf, "I=%.4f", m->moment_of_inertia);
            d->draw_text(0, 36, buf, 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            d->draw_text(0, 50, m->is_active ? "ACTIVE" : "IDLE", 1,
                         HW_COLOR_WHITE, HW_COLOR_BLACK);
            break;
        default: {
            uint32_t s = m->elapsed_time_ms / 1000;
            snprintf(buf, sizeof buf, "%02lu:%02lu:%02lu",
                     (unsigned long)(s / 3600),
                     (unsigned long)((s / 60) % 60),
                     (unsigned long)(s % 60));
            d->draw_text(0, 0, "TIME", 1, HW_COLOR_WHITE, HW_COLOR_BLACK);
            d->draw_text(0, 16, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        } break;
        }
        return;
    }

    /* COLOR ---------------------------------------------------------- */
    hw_color_t accent = m->is_paused ? HW_COLOR_YELLOW
                       : (m->is_active ? HW_COLOR_GREEN : HW_COLOR_GRAY);

    switch (screen_idx) {
    case 0:
        format_pace(m->instantaneous_pace_sec_500m, pace, sizeof pace);
        d->draw_text(4, 4, "/500m", 1, HW_COLOR_GRAY, HW_COLOR_BLACK);
        d->draw_text(4, 18, pace, 4, accent, HW_COLOR_BLACK);
        d->draw_text(d->width / 2 + 4, 4, "POWER", 1, HW_COLOR_GRAY, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "%.0fW", m->display_power_watts);
        d->draw_text(d->width / 2 + 4, 18, buf, 3, HW_COLOR_WHITE, HW_COLOR_BLACK);
        d->draw_text(4, d->height / 2 + 4, "DIST", 1, HW_COLOR_GRAY, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "%.0fm", m->total_distance_meters);
        d->draw_text(4, d->height / 2 + 18, buf, 3, HW_COLOR_WHITE, HW_COLOR_BLACK);
        d->draw_text(d->width / 2 + 4, d->height / 2 + 4, "RATE",
                     1, HW_COLOR_GRAY, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "%.0fspm", m->stroke_rate_spm);
        d->draw_text(d->width / 2 + 4, d->height / 2 + 18, buf, 3,
                     HW_COLOR_CYAN, HW_COLOR_BLACK);
        break;

    case 1: {
        d->draw_text(4, 4, "TOTALS", 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        uint32_t s = m->elapsed_time_ms / 1000;
        snprintf(buf, sizeof buf, "Time   %02lu:%02lu:%02lu",
                 (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60),
                 (unsigned long)(s % 60));
        d->draw_text(4, 36, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "Dist  %.0f m", m->total_distance_meters);
        d->draw_text(4, 60, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "Strk  %lu", (unsigned long)m->stroke_count);
        d->draw_text(4, 84, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "AvgP  %.0f W", m->average_power_watts);
        d->draw_text(4, 108, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "Cal   %lu", (unsigned long)m->total_calories);
        d->draw_text(4, 132, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        if (m->heart_rate_bpm) {
            snprintf(buf, sizeof buf, "HR    %u", m->heart_rate_bpm);
            d->draw_text(4, 156, buf, 2, HW_COLOR_RED, HW_COLOR_BLACK);
        }
    } break;

    case 2:
        d->draw_text(4, 4, "DRAG / CALIB", 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "Drag : %.1f", m->drag_factor);
        d->draw_text(4, 36, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "I=%.4f", m->moment_of_inertia);
        d->draw_text(4, 60, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "Samples: %lu",
                 (unsigned long)m->drag_calibration_samples);
        d->draw_text(4, 84, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        d->draw_text(4, 108,
                     m->calibration_complete ? "CAL: COMPLETE" : "CAL: RUN",
                     2, accent, HW_COLOR_BLACK);
        break;

    default:
        d->draw_text(4, 4, "STATUS", 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        d->draw_text(4, 36, m->is_active ? "ACTIVE" : "IDLE",
                     3, accent, HW_COLOR_BLACK);
        snprintf(buf, sizeof buf, "Phase: %s",
                 m->current_phase == 1 ? "DRIVE" :
                 m->current_phase == 2 ? "RECOV" : "IDLE");
        d->draw_text(4, 80, buf, 2, HW_COLOR_WHITE, HW_COLOR_BLACK);
        if (m->is_paused) {
            d->draw_text(4, 110, "PAUSED", 3, HW_COLOR_YELLOW, HW_COLOR_BLACK);
        }
        break;
    }
}

#endif /* CONFIG_ROWING_DISPLAY_ENABLED */
