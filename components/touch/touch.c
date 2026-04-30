/**
 * @file touch.c
 * @brief Touch HAL dispatcher + polling task.
 */

#include "touch.h"
#include "drivers/touch_driver.h"
#include "display.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "touch";

static const touch_driver_t *s_drv = NULL;
static touch_cb_t            s_cb  = NULL;
static void                 *s_user = NULL;
static TaskHandle_t          s_task = NULL;
static volatile bool         s_run = false;

#if CONFIG_ROWING_TOUCH_ENABLED

static void map_coords(int16_t rx, int16_t ry, int16_t *out_x, int16_t *out_y)
{
    int16_t x = rx, y = ry;
#if CONFIG_ROWING_TOUCH_SWAP_XY
    int16_t tmp = x; x = y; y = tmp;
#endif
#if CONFIG_ROWING_TOUCH_INVERT_X
    x = (int16_t)(s_drv->raw_max_x - x);
#endif
#if CONFIG_ROWING_TOUCH_INVERT_Y
    y = (int16_t)(s_drv->raw_max_y - y);
#endif
    uint16_t dw_raw = display_width();
    uint16_t dh_raw = display_height();
    uint16_t dw = dw_raw ? dw_raw : 240;
    uint16_t dh = dh_raw ? dh_raw : 240;
    uint16_t mx = s_drv->raw_max_x ? s_drv->raw_max_x : 4095;
    uint16_t my = s_drv->raw_max_y ? s_drv->raw_max_y : 4095;
    *out_x = (int16_t)((int32_t)x * dw / mx);
    *out_y = (int16_t)((int32_t)y * dh / my);
}

static void touch_task(void *arg)
{
    (void)arg;
    bool was_pressed = false;
    int16_t lx = -1, ly = -1;
    const TickType_t period = pdMS_TO_TICKS(1000 / CONFIG_ROWING_TOUCH_POLL_HZ);

    while (s_run) {
        int16_t rx = 0, ry = 0;
        bool pressed = s_drv->read(&rx, &ry);
        if (pressed) {
            int16_t x = 0, y = 0;
            map_coords(rx, ry, &x, &y);
            lx = x; ly = y;
            if (s_cb) {
                touch_event_t e = { .x = x, .y = y, .pressed = true };
                s_cb(&e, s_user);
            }
            was_pressed = true;
        } else if (was_pressed) {
            if (s_cb) {
                touch_event_t e = { .x = lx, .y = ly, .pressed = false };
                s_cb(&e, s_user);
            }
            was_pressed = false;
        }
        vTaskDelay(period);
    }
    vTaskDelete(NULL);
}

#endif /* CONFIG_ROWING_TOUCH_ENABLED */

esp_err_t touch_init(void)
{
#if !CONFIG_ROWING_TOUCH_ENABLED
    return ESP_OK;
#else
  #if CONFIG_ROWING_TOUCH_XPT2046
    s_drv = touch_driver_xpt2046_get();
  #else
    s_drv = touch_driver_i2c_get();
  #endif
    if (!s_drv) return ESP_ERR_NOT_FOUND;
    esp_err_t e = s_drv->init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Touch driver %s init: 0x%x", s_drv->name, e);
        s_drv = NULL;
        return e;
    }
    s_run = true;
    BaseType_t r = xTaskCreate(touch_task, "touch", 3072, NULL, 4, &s_task);
    if (r != pdPASS) {
        s_run = false;
        if (s_drv->deinit) s_drv->deinit();
        s_drv = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Touch ready: %s", s_drv->name);
    return ESP_OK;
#endif
}

void touch_set_callback(touch_cb_t cb, void *user) { s_cb = cb; s_user = user; }
bool touch_is_ready(void) { return s_drv != NULL; }

void touch_deinit(void)
{
    s_run = false;
    if (s_drv && s_drv->deinit) s_drv->deinit();
    s_drv = NULL;
    s_task = NULL;
}
