/**
 * @file touch_driver.h
 * @brief Internal driver vtable for touch backends.
 */

#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    void      (*deinit)(void);
    bool      (*read)(int16_t *x_raw, int16_t *y_raw);
    uint16_t   raw_max_x;
    uint16_t   raw_max_y;
} touch_driver_t;

#if CONFIG_ROWING_TOUCH_XPT2046
const touch_driver_t *touch_driver_xpt2046_get(void);
#endif
#if CONFIG_ROWING_TOUCH_GT911 || CONFIG_ROWING_TOUCH_FT5X06 || CONFIG_ROWING_TOUCH_CST816S
const touch_driver_t *touch_driver_i2c_get(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_DRIVER_H */
