/**
 * @file input_encoder.c
 * @brief PCNT-based quadrature rotary encoder + optional push button.
 *
 * Uses the IDF v5 `driver/pulse_cnt.h` API. A polling task reads the
 * accumulated count and emits CW/CCW actions in detent units.
 */

#include "input_encoder.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_ROWING_ENCODER_ENABLED

static const char *TAG = "encoder";

static pcnt_unit_handle_t    s_unit  = NULL;
static pcnt_channel_handle_t s_chan_a = NULL;
static pcnt_channel_handle_t s_chan_b = NULL;
static TaskHandle_t          s_task  = NULL;
static volatile bool         s_run   = false;

static ui_action_t           s_cw  = (ui_action_t)CONFIG_ROWING_ENCODER_CW_ACTION;
static ui_action_t           s_ccw = (ui_action_t)CONFIG_ROWING_ENCODER_CCW_ACTION;
static ui_action_t           s_btn = (ui_action_t)CONFIG_ROWING_ENCODER_BTN_ACTION;

static void enc_task(void *arg)
{
    (void)arg;
    int   last_count = 0;
    bool  btn_was = false;
    int64_t btn_change_us = 0;

    pcnt_unit_get_count(s_unit, &last_count);

    while (s_run) {
        int now_count = 0;
        pcnt_unit_get_count(s_unit, &now_count);
        int delta = now_count - last_count;
        last_count = now_count;

#if CONFIG_ROWING_ENCODER_INVERT
        delta = -delta;
#endif

        int per_detent = CONFIG_ROWING_ENCODER_STEPS_PER_DETENT;
        int detents = delta / per_detent;
        if (detents > 0) {
            for (int i = 0; i < detents; ++i) ui_actions_post(s_cw);
            /* Adjust to keep modulo */
            last_count -= detents * per_detent;
            pcnt_unit_clear_count(s_unit);
        } else if (detents < 0) {
            for (int i = 0; i < -detents; ++i) ui_actions_post(s_ccw);
            last_count -= detents * per_detent;
            pcnt_unit_clear_count(s_unit);
        }

#if CONFIG_ROWING_ENCODER_PIN_BTN >= 0
        bool pressed = gpio_get_level((gpio_num_t)CONFIG_ROWING_ENCODER_PIN_BTN) == 0;
        int64_t now = esp_timer_get_time();
        if (pressed != btn_was) {
            if ((now - btn_change_us) / 1000 > 25) {        /* 25 ms debounce */
                btn_was = pressed;
                btn_change_us = now;
                if (pressed && s_btn != UI_ACTION_NONE) {
                    ui_actions_post(s_btn);
                }
            }
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

#endif /* CONFIG_ROWING_ENCODER_ENABLED */

esp_err_t input_encoder_init(void)
{
#if !CONFIG_ROWING_ENCODER_ENABLED
    return ESP_OK;
#else
    if (CONFIG_ROWING_ENCODER_PIN_A < 0 || CONFIG_ROWING_ENCODER_PIN_B < 0) {
        ESP_LOGW(TAG, "encoder enabled but A/B pins unset");
        return ESP_OK;
    }
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 1000,
        .low_limit  = -1000,
    };
    esp_err_t e = pcnt_new_unit(&unit_cfg, &s_unit);
    if (e != ESP_OK) return e;
    pcnt_glitch_filter_config_t flt = { .max_glitch_ns = 1000 };
    pcnt_unit_set_glitch_filter(s_unit, &flt);

    pcnt_chan_config_t a_cfg = {
        .edge_gpio_num  = CONFIG_ROWING_ENCODER_PIN_A,
        .level_gpio_num = CONFIG_ROWING_ENCODER_PIN_B,
    };
    pcnt_chan_config_t b_cfg = {
        .edge_gpio_num  = CONFIG_ROWING_ENCODER_PIN_B,
        .level_gpio_num = CONFIG_ROWING_ENCODER_PIN_A,
    };
    pcnt_new_channel(s_unit, &a_cfg, &s_chan_a);
    pcnt_new_channel(s_unit, &b_cfg, &s_chan_b);

    pcnt_channel_set_edge_action(s_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(s_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(s_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(s_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(s_unit);
    pcnt_unit_clear_count(s_unit);
    pcnt_unit_start(s_unit);

#if CONFIG_ROWING_ENCODER_PIN_BTN >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_ROWING_ENCODER_PIN_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
#endif

    s_run = true;
    if (xTaskCreate(enc_task, "encoder", 3072, NULL, 4, &s_task) != pdPASS) {
        s_run = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Encoder ready (A=%d B=%d btn=%d)",
             CONFIG_ROWING_ENCODER_PIN_A, CONFIG_ROWING_ENCODER_PIN_B,
             CONFIG_ROWING_ENCODER_PIN_BTN);
    return ESP_OK;
#endif
}

void input_encoder_set_actions(ui_action_t cw, ui_action_t ccw, ui_action_t btn)
{
#if CONFIG_ROWING_ENCODER_ENABLED
    if (cw  != UI_ACTION_NONE) s_cw  = cw;
    if (ccw != UI_ACTION_NONE) s_ccw = ccw;
    if (btn != UI_ACTION_NONE) s_btn = btn;
#else
    (void)cw; (void)ccw; (void)btn;
#endif
}

void input_encoder_deinit(void)
{
#if CONFIG_ROWING_ENCODER_ENABLED
    s_run = false;
    if (s_unit) {
        pcnt_unit_stop(s_unit);
        pcnt_unit_disable(s_unit);
        if (s_chan_a) pcnt_del_channel(s_chan_a);
        if (s_chan_b) pcnt_del_channel(s_chan_b);
        pcnt_del_unit(s_unit);
        s_unit = NULL; s_chan_a = NULL; s_chan_b = NULL;
    }
#endif
}
