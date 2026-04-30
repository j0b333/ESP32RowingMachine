/**
 * @file touch.h
 * @brief Touch HAL — uniform API across all supported controllers.
 *
 * The driver runs in its own polling task. Press / release events are
 * delivered via the registered callback with coordinates already mapped
 * to display pixels (and rotation/inversion applied per Kconfig).
 */

#ifndef TOUCH_H
#define TOUCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hw_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*touch_cb_t)(const touch_event_t *evt, void *user);

/** Initialize the configured touch driver. No-op when disabled. */
esp_err_t touch_init(void);

/** Register a callback invoked on every press / release. */
void touch_set_callback(touch_cb_t cb, void *user);

/** True if a touch driver was successfully initialized. */
bool touch_is_ready(void);

/** Stop the polling task and release driver resources. */
void touch_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_H */
