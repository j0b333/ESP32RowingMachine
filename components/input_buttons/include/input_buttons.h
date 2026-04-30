/**
 * @file input_buttons.h
 * @brief Programmable GPIO button manager (short/long/double press → action).
 */

#ifndef INPUT_BUTTONS_H
#define INPUT_BUTTONS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hw_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_BUTTONS_MAX 4

/** Per-button mapping. Set `gpio = -1` to disable. */
typedef struct {
    int8_t      gpio;
    ui_action_t short_action;
    ui_action_t long_action;
    ui_action_t double_action;
} button_map_t;

/** Initialize buttons from Kconfig defaults. Safe no-op if disabled. */
esp_err_t input_buttons_init(void);

/** Install or replace the global button map at runtime. */
esp_err_t input_buttons_set_map(const button_map_t *map, size_t count);

/** Read current map (for serializing to web UI). */
size_t input_buttons_get_map(button_map_t *out, size_t max);

/** Stop button polling and release resources. */
void input_buttons_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_BUTTONS_H */
