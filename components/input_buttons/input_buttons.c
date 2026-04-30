/**
 * @file input_buttons.c
 * @brief Polling-based programmable button manager with short / long /
 *        double-press detection. Detected gestures are translated into
 *        UI actions and posted to the action dispatcher.
 */

#include "input_buttons.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "buttons";

#if CONFIG_ROWING_BUTTONS_ENABLED

typedef struct {
    button_map_t map;
    bool         level;             /* current debounced level (true=pressed) */
    bool         raw_level;
    int64_t      level_change_us;
    int64_t      press_start_us;
    int64_t      last_release_us;
    bool         long_fired;
    bool         pending_single;    /* short-press waiting to confirm not a double */
    int64_t      pending_single_us;
} btn_state_t;

static btn_state_t s_btns[INPUT_BUTTONS_MAX];
static size_t     s_count = 0;
static TaskHandle_t s_task = NULL;
static volatile bool s_run = false;

static inline bool gpio_active(int pin)
{
    int lvl = gpio_get_level((gpio_num_t)pin);
#if CONFIG_ROWING_BUTTONS_ACTIVE_LOW
    return lvl == 0;
#else
    return lvl != 0;
#endif
}

static void install_pin(int pin)
{
    if (pin < 0) return;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
#if CONFIG_ROWING_BUTTONS_INTERNAL_PULLUP && CONFIG_ROWING_BUTTONS_ACTIVE_LOW
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = 0,
#elif CONFIG_ROWING_BUTTONS_INTERNAL_PULLUP
        .pull_up_en = 0,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
#else
        .pull_up_en = 0,
        .pull_down_en = 0,
#endif
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static void process_button(btn_state_t *b, int64_t now)
{
    bool raw = gpio_active(b->map.gpio);
    if (raw != b->raw_level) {
        b->raw_level = raw;
        b->level_change_us = now;
    }
    if ((now - b->level_change_us) / 1000 >= CONFIG_ROWING_BUTTONS_DEBOUNCE_MS &&
        b->level != b->raw_level) {
        b->level = b->raw_level;
        if (b->level) {
            b->press_start_us = now;
            b->long_fired = false;
        } else {
            int64_t held_ms = (now - b->press_start_us) / 1000;
            if (b->long_fired) {
                /* long-press already dispatched on hold */
            } else if (held_ms >= 0) {
                /* Short press: defer to detect possible double */
                if (b->pending_single &&
                    (now - b->pending_single_us) / 1000
                        <= CONFIG_ROWING_BUTTONS_DOUBLE_MS) {
                    /* Second click within window: dispatch double */
                    b->pending_single = false;
                    if (b->map.double_action != UI_ACTION_NONE) {
                        ui_actions_post(b->map.double_action);
                    }
                } else {
                    b->pending_single = true;
                    b->pending_single_us = now;
                }
            }
            b->last_release_us = now;
        }
    }

    /* Long-press detection while held */
    if (b->level && !b->long_fired &&
        (now - b->press_start_us) / 1000 >= CONFIG_ROWING_BUTTONS_LONG_MS) {
        b->long_fired = true;
        b->pending_single = false;          /* a long press is not a short */
        if (b->map.long_action != UI_ACTION_NONE) {
            ui_actions_post(b->map.long_action);
        }
    }

    /* Confirm a deferred short press once the double-click window expires. */
    if (b->pending_single &&
        (now - b->pending_single_us) / 1000 > CONFIG_ROWING_BUTTONS_DOUBLE_MS) {
        b->pending_single = false;
        if (b->map.short_action != UI_ACTION_NONE) {
            ui_actions_post(b->map.short_action);
        }
    }
}

static void buttons_task(void *arg)
{
    (void)arg;
    while (s_run) {
        int64_t now = esp_timer_get_time();
        for (size_t i = 0; i < s_count; ++i) {
            if (s_btns[i].map.gpio >= 0) process_button(&s_btns[i], now);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

#endif /* CONFIG_ROWING_BUTTONS_ENABLED */

esp_err_t input_buttons_init(void)
{
#if !CONFIG_ROWING_BUTTONS_ENABLED
    return ESP_OK;
#else
    memset(s_btns, 0, sizeof s_btns);

    const button_map_t defaults[INPUT_BUTTONS_MAX] = {
        { .gpio = CONFIG_ROWING_BUTTON1_PIN,
          .short_action  = (ui_action_t)CONFIG_ROWING_BUTTON1_SHORT_ACTION,
          .long_action   = (ui_action_t)CONFIG_ROWING_BUTTON1_LONG_ACTION,
          .double_action = (ui_action_t)CONFIG_ROWING_BUTTON1_DOUBLE_ACTION },
        { .gpio = CONFIG_ROWING_BUTTON2_PIN,
          .short_action  = (ui_action_t)CONFIG_ROWING_BUTTON2_SHORT_ACTION,
          .long_action   = (ui_action_t)CONFIG_ROWING_BUTTON2_LONG_ACTION,
          .double_action = (ui_action_t)CONFIG_ROWING_BUTTON2_DOUBLE_ACTION },
        { .gpio = CONFIG_ROWING_BUTTON3_PIN,
          .short_action  = (ui_action_t)CONFIG_ROWING_BUTTON3_SHORT_ACTION,
          .long_action   = (ui_action_t)CONFIG_ROWING_BUTTON3_LONG_ACTION,
          .double_action = (ui_action_t)CONFIG_ROWING_BUTTON3_DOUBLE_ACTION },
        { .gpio = CONFIG_ROWING_BUTTON4_PIN,
          .short_action  = (ui_action_t)CONFIG_ROWING_BUTTON4_SHORT_ACTION,
          .long_action   = (ui_action_t)CONFIG_ROWING_BUTTON4_LONG_ACTION,
          .double_action = (ui_action_t)CONFIG_ROWING_BUTTON4_DOUBLE_ACTION },
    };

    s_count = 0;
    for (size_t i = 0; i < CONFIG_ROWING_BUTTONS_COUNT && i < INPUT_BUTTONS_MAX; ++i) {
        s_btns[i].map = defaults[i];
        install_pin(s_btns[i].map.gpio);
        s_count++;
    }
    if (s_count == 0) return ESP_OK;

    s_run = true;
    BaseType_t r = xTaskCreate(buttons_task, "buttons", 3072, NULL, 5, &s_task);
    if (r != pdPASS) {
        s_run = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Buttons ready: %u", (unsigned)s_count);
    return ESP_OK;
#endif
}

esp_err_t input_buttons_set_map(const button_map_t *map, size_t count)
{
#if !CONFIG_ROWING_BUTTONS_ENABLED
    (void)map; (void)count;
    return ESP_OK;
#else
    if (!map) return ESP_ERR_INVALID_ARG;
    if (count > INPUT_BUTTONS_MAX) count = INPUT_BUTTONS_MAX;
    for (size_t i = 0; i < count; ++i) {
        s_btns[i].map = map[i];
        install_pin(s_btns[i].map.gpio);
    }
    s_count = count;
    return ESP_OK;
#endif
}

size_t input_buttons_get_map(button_map_t *out, size_t max)
{
#if !CONFIG_ROWING_BUTTONS_ENABLED
    (void)out; (void)max;
    return 0;
#else
    if (!out) return 0;
    size_t n = s_count < max ? s_count : max;
    for (size_t i = 0; i < n; ++i) out[i] = s_btns[i].map;
    return n;
#endif
}

void input_buttons_deinit(void)
{
#if CONFIG_ROWING_BUTTONS_ENABLED
    s_run = false;
    s_task = NULL;
    s_count = 0;
#endif
}
