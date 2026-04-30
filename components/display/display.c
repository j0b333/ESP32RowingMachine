/**
 * @file display.c
 * @brief Display HAL dispatcher.
 */

#include "display.h"
#include "drivers/display_driver.h"

#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "display";

static const display_driver_t *s_drv = NULL;
static bool     s_ready = false;
static uint8_t  s_backlight = 0;
static uint8_t  s_screen = 0;
static uint8_t  s_screen_count = 1;

#if CONFIG_ROWING_DISPLAY_ENABLED
extern uint8_t display_renderer_screen_count(void);
extern void    display_renderer_draw_metrics(const display_driver_t *drv,
                                             const display_metrics_t *m,
                                             uint8_t screen_idx);
extern void    display_renderer_draw_status(const display_driver_t *drv,
                                            const char *l1, const char *l2,
                                            const char *l3, const char *l4);
extern void    display_renderer_draw_splash(const display_driver_t *drv,
                                            const char *version);
#endif

esp_err_t display_init(void)
{
#if !CONFIG_ROWING_DISPLAY_ENABLED
    ESP_LOGI(TAG, "Display disabled at compile time");
    return ESP_OK;
#else
  #if CONFIG_ROWING_DISPLAY_SSD1306 || CONFIG_ROWING_DISPLAY_SH1106
    s_drv = display_driver_oled_get();
  #elif CONFIG_ROWING_DISPLAY_ST7789 || CONFIG_ROWING_DISPLAY_ILI9341 || \
        CONFIG_ROWING_DISPLAY_ILI9488 || CONFIG_ROWING_DISPLAY_GC9A01 || \
        CONFIG_ROWING_DISPLAY_ST7796
    s_drv = display_driver_spi_tft_get();
  #endif
    if (!s_drv) {
        ESP_LOGE(TAG, "No display driver available");
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t r = s_drv->init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Driver %s init failed: 0x%x", s_drv->name, r);
        s_drv = NULL;
        return r;
    }
    s_ready = true;
    s_backlight = CONFIG_ROWING_DISPLAY_DEFAULT_BACKLIGHT;
    if (s_drv->set_backlight) {
        s_drv->set_backlight(s_backlight);
    }
    s_drv->clear(HW_COLOR_BLACK);
    s_drv->flush();
    s_screen_count = display_renderer_screen_count();
    ESP_LOGI(TAG, "Display ready: %s %dx%d",
             s_drv->name, s_drv->width, s_drv->height);
    return ESP_OK;
#endif
}

void display_deinit(void)
{
    if (s_drv && s_drv->deinit) s_drv->deinit();
    s_drv = NULL;
    s_ready = false;
}

bool     display_is_ready(void)  { return s_ready; }
uint16_t display_width(void)     { return s_drv ? s_drv->width  : 0; }
uint16_t display_height(void)    { return s_drv ? s_drv->height : 0; }

esp_err_t display_set_backlight(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_backlight = pct;
    if (s_drv && s_drv->set_backlight) return s_drv->set_backlight(pct);
    return ESP_OK;
}
uint8_t display_get_backlight(void) { return s_backlight; }

void display_render_splash(const char *version)
{
#if CONFIG_ROWING_DISPLAY_ENABLED
    if (!s_drv) return;
    display_renderer_draw_splash(s_drv, version ? version : "");
    s_drv->flush();
#else
    (void)version;
#endif
}

void display_render_status(const char *l1, const char *l2,
                           const char *l3, const char *l4)
{
#if CONFIG_ROWING_DISPLAY_ENABLED
    if (!s_drv) return;
    display_renderer_draw_status(s_drv,
        l1 ? l1 : "", l2 ? l2 : "", l3 ? l3 : "", l4 ? l4 : "");
    s_drv->flush();
#else
    (void)l1; (void)l2; (void)l3; (void)l4;
#endif
}

void display_render_metrics(const display_metrics_t *m)
{
#if CONFIG_ROWING_DISPLAY_ENABLED
    if (!s_drv || !m) return;
    display_renderer_draw_metrics(s_drv, m, s_screen);
    s_drv->flush();
#else
    (void)m;
#endif
}

void display_next_screen(void)
{
    if (s_screen_count == 0) return;
    s_screen = (uint8_t)((s_screen + 1) % s_screen_count);
}
void display_prev_screen(void)
{
    if (s_screen_count == 0) return;
    s_screen = (uint8_t)((s_screen + s_screen_count - 1) % s_screen_count);
}
void display_set_screen(uint8_t idx)
{
    if (s_screen_count && idx < s_screen_count) s_screen = idx;
}
uint8_t display_get_screen(void)        { return s_screen; }
uint8_t display_get_screen_count(void)  { return s_screen_count; }
