/**
 * @file hardware.c
 * @brief HAL integration glue. See hardware.h.
 */

#include "hardware.h"

#include "esp_log.h"
#include "sdkconfig.h"

/* HAL component headers — always present even when disabled, because
 * each component compiles to a stub when its Kconfig flag is off. */
#include "hw_hal.h"
#include "board.h"
#include "display.h"
#include "touch.h"
#include "input_buttons.h"
#include "input_encoder.h"
#include "audio.h"
#include "indicator_led.h"
#include "app_config.h"

static const char *TAG = "hardware";

static rowing_metrics_t *s_metrics = NULL;
static config_t         *s_cfg     = NULL;

/* ------------------------------------------------------------------ */
/*                       UI ACTION HANDLER                             */
/* ------------------------------------------------------------------ */

static void on_action(ui_action_t a, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "Action: %s", hw_hal_action_name(a));

    switch (a) {
    case UI_ACTION_NEXT_SCREEN:        display_next_screen(); break;
    case UI_ACTION_PREV_SCREEN:        display_prev_screen(); break;
    case UI_ACTION_SCREEN_HOME:        display_set_screen(0); break;
    case UI_ACTION_BRIGHTNESS_UP: {
        uint8_t v = display_get_backlight();
        v = (v + 10 > 100) ? 100 : v + 10;
        display_set_backlight(v);
        break;
    }
    case UI_ACTION_BRIGHTNESS_DOWN: {
        uint8_t v = display_get_backlight();
        v = (v < 10) ? 0 : v - 10;
        display_set_backlight(v);
        break;
    }
    case UI_ACTION_VOLUME_UP: {
        uint8_t v = audio_get_volume();
        v = (v + 10 > 100) ? 100 : v + 10;
        audio_set_volume(v);
        audio_beep(AUDIO_BEEP_TICK);
        break;
    }
    case UI_ACTION_VOLUME_DOWN: {
        uint8_t v = audio_get_volume();
        v = (v < 10) ? 0 : v - 10;
        audio_set_volume(v);
        audio_beep(AUDIO_BEEP_TICK);
        break;
    }
    case UI_ACTION_MUTE_TOGGLE: {
        bool m = !audio_is_muted();
        audio_set_muted(m);
        if (!m) audio_beep(AUDIO_BEEP_OK);
        break;
    }
    case UI_ACTION_START_STOP:
    case UI_ACTION_PAUSE_RESUME:
    case UI_ACTION_LAP:
    case UI_ACTION_RESET_SESSION:
        /* These require coordination with session_manager. For now we
         * just emit a beep; the session_manager already has its own
         * activity-driven start/stop. A future change can wire these
         * into session_manager_force_start/stop/lap. */
        audio_beep(AUDIO_BEEP_OK);
        break;
    case UI_ACTION_TOGGLE_HR_SCAN:
    case UI_ACTION_WIFI_PROVISION:
    case UI_ACTION_BLE_TOGGLE:
        audio_beep(AUDIO_BEEP_WARN);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */

esp_err_t hardware_init(rowing_metrics_t *metrics, config_t *cfg)
{
    s_metrics = metrics;
    s_cfg     = cfg;

    ESP_LOGI(TAG, "Board: %s", board_get_name());

    esp_err_t first_err = ESP_OK;
    esp_err_t e;

#define TRY(call) do { e = (call); if (e != ESP_OK && first_err == ESP_OK) first_err = e; } while (0)

    TRY(ui_actions_init());
    ui_actions_set_handler(on_action, NULL);

    TRY(indicator_led_init());
    TRY(display_init());
    TRY(touch_init());
    TRY(input_buttons_init());
    TRY(input_encoder_init());
    TRY(audio_init());

#undef TRY

    if (display_is_ready()) {
        display_render_splash(APP_VERSION_STRING);
    }

    if (audio_is_ready()) {
        audio_beep(AUDIO_BEEP_OK);
    }

    return first_err;
}

void hardware_render_metrics(const rowing_metrics_t *m)
{
    if (!m || !display_is_ready()) return;
    display_metrics_t v = {
        .instantaneous_pace_sec_500m = m->instantaneous_pace_sec_500m,
        .average_pace_sec_500m       = m->average_pace_sec_500m,
        .display_power_watts         = m->display_power_watts,
        .average_power_watts         = m->average_power_watts,
        .stroke_rate_spm             = m->stroke_rate_spm,
        .total_distance_meters       = m->total_distance_meters,
        .drag_factor                 = m->drag_factor,
        .moment_of_inertia           = m->moment_of_inertia,
        .elapsed_time_ms             = (uint32_t)m->elapsed_time_ms,
        .stroke_count                = m->stroke_count,
        .total_calories              = (uint32_t)m->total_calories,
        .drag_calibration_samples    = m->drag_calibration_samples,
        .heart_rate_bpm              = m->heart_rate_bpm,
        .current_phase               = (uint8_t)m->current_phase,
        .is_active                   = m->is_active,
        .is_paused                   = m->is_paused,
        .calibration_complete        = m->calibration_complete,
    };
    display_render_metrics(&v);
}

void hardware_update_indicator(const rowing_metrics_t *m,
                               bool wifi_connected,
                               bool ble_advertising,
                               bool hr_connected)
{
    led_state_t s;
    if (!m)                    s = LED_STATE_OFF;
    else if (m->is_paused)     s = LED_STATE_PAUSED;
    else if (m->is_active)     s = LED_STATE_ACTIVE;
    else if (hr_connected)     s = LED_STATE_HR_CONNECTED;
    else if (ble_advertising)  s = LED_STATE_BLE_ADVERTISING;
    else if (!wifi_connected)  s = LED_STATE_WIFI_PROVISIONING;
    else                       s = LED_STATE_IDLE;
    indicator_led_set(s);
}

void hardware_on_stroke(void)
{
#if CONFIG_ROWING_AUDIO_BEEP_STROKE
    audio_beep(AUDIO_BEEP_STROKE);
#endif
}

void hardware_on_interval(void)
{
#if CONFIG_ROWING_AUDIO_BEEP_INTERVALS
    audio_beep(AUDIO_BEEP_INTERVAL);
#endif
}

void hardware_deinit(void)
{
    audio_deinit();
    input_encoder_deinit();
    input_buttons_deinit();
    touch_deinit();
    display_deinit();
    indicator_led_deinit();
    s_metrics = NULL;
    s_cfg = NULL;
}
