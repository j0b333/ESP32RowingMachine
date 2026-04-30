/**
 * @file display_driver.h
 * @brief Internal driver vtable for display backends.
 */

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hw_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint16_t    width;
    uint16_t    height;
    bool        color;          /* true if RGB565 backend, false for mono */

    esp_err_t (*init)(void);
    void      (*deinit)(void);
    esp_err_t (*set_backlight)(uint8_t pct);

    void      (*clear)(hw_color_t color);
    void      (*flush)(void);

    void      (*fill_rect)(int16_t x, int16_t y, int16_t w, int16_t h, hw_color_t color);
    void      (*draw_pixel)(int16_t x, int16_t y, hw_color_t color);
    void      (*draw_text)(int16_t x, int16_t y, const char *text,
                            uint8_t scale, hw_color_t color, hw_color_t bg);
} display_driver_t;

#if CONFIG_ROWING_DISPLAY_SSD1306 || CONFIG_ROWING_DISPLAY_SH1106
const display_driver_t *display_driver_oled_get(void);
#endif
#if CONFIG_ROWING_DISPLAY_ST7789 || CONFIG_ROWING_DISPLAY_ILI9341 || \
    CONFIG_ROWING_DISPLAY_ILI9488 || CONFIG_ROWING_DISPLAY_GC9A01 || \
    CONFIG_ROWING_DISPLAY_ST7796
const display_driver_t *display_driver_spi_tft_get(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_DRIVER_H */
