/**
 * @file input_encoder.h
 * @brief Quadrature rotary encoder + push-button HAL.
 */

#ifndef INPUT_ENCODER_H
#define INPUT_ENCODER_H

#include "esp_err.h"
#include "hw_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize encoder hardware (PCNT) per Kconfig. No-op when disabled. */
esp_err_t input_encoder_init(void);

/** Override actions at runtime. Pass UI_ACTION_NONE to leave unchanged. */
void input_encoder_set_actions(ui_action_t cw, ui_action_t ccw, ui_action_t btn);

void input_encoder_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
