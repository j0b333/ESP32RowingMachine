/**
 * @file display_spi_tft.c
 * @brief Generic SPI TFT driver: ST7789 / ILI9341 / ILI9488 / GC9A01 / ST7796.
 *
 * A single TU covers all four controllers because they share the
 * MIPI-DCS command set; only the init sequence differs slightly. Init
 * differences are handled in `init_panel()` based on Kconfig.
 *
 * Renders pixels by streaming straight to the panel — no framebuffer.
 * Backlight is driven via LEDC PWM if a BL pin is configured.
 *
 * Pin defaults are taken from the board profile if the per-component
 * Kconfig pin is left at -1 (the sentinel "use board default").
 */

#include "sdkconfig.h"

#if CONFIG_ROWING_DISPLAY_ST7789 || CONFIG_ROWING_DISPLAY_ILI9341 || \
    CONFIG_ROWING_DISPLAY_ILI9488 || CONFIG_ROWING_DISPLAY_GC9A01 || \
    CONFIG_ROWING_DISPLAY_ST7796

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display_driver.h"
#include "display_font5x7.h"
#include "board.h"

static const char *TAG = "display_tft";

#define TFT_W  CONFIG_ROWING_DISPLAY_WIDTH
#define TFT_H  CONFIG_ROWING_DISPLAY_HEIGHT

/* Resolve pins: prefer per-component Kconfig if set, else board default. */
#define _DISP_RESOLVE(cfg, brd) ((cfg) >= 0 ? (cfg) : (brd))
#define PIN_CS   _DISP_RESOLVE(CONFIG_ROWING_DISPLAY_PIN_CS,  BOARD_DISPLAY_CS)
#define PIN_DC   _DISP_RESOLVE(CONFIG_ROWING_DISPLAY_PIN_DC,  BOARD_DISPLAY_DC)
#define PIN_RST  _DISP_RESOLVE(CONFIG_ROWING_DISPLAY_PIN_RST, BOARD_DISPLAY_RST)
#define PIN_BL   _DISP_RESOLVE(CONFIG_ROWING_DISPLAY_PIN_BL,  BOARD_DISPLAY_BL)

#if CONFIG_ROWING_DISPLAY_ILI9488
  #define PIXEL_BYTES 3
#else
  #define PIXEL_BYTES 2
#endif

static spi_device_handle_t s_spi = NULL;
static bool                s_bl_pwm_ready = false;

static inline void cs_low(void)  { if (PIN_CS >= 0) gpio_set_level((gpio_num_t)PIN_CS, 0); }
static inline void cs_high(void) { if (PIN_CS >= 0) gpio_set_level((gpio_num_t)PIN_CS, 1); }

static void spi_write(const uint8_t *data, size_t len, bool is_cmd)
{
    if (len == 0) return;
    if (PIN_DC >= 0) gpio_set_level((gpio_num_t)PIN_DC, is_cmd ? 0 : 1);
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    cs_low();
    spi_device_polling_transmit(s_spi, &t);
    cs_high();
}

static void wcmd(uint8_t c)            { spi_write(&c, 1, true); }
static void wdata(const uint8_t *d, size_t n) { spi_write(d, n, false); }
static void wbyte(uint8_t b)           { spi_write(&b, 1, false); }

static void hw_reset(void)
{
    if (PIN_RST < 0) return;
    gpio_set_level((gpio_num_t)PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];
    wcmd(0x2A);
    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF; buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    wdata(buf, 4);
    wcmd(0x2B);
    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF; buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    wdata(buf, 4);
    wcmd(0x2C);
}

static void init_panel(void)
{
    hw_reset();
    wcmd(0x01); vTaskDelay(pdMS_TO_TICKS(120));
    wcmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));

#if CONFIG_ROWING_DISPLAY_ILI9488
    wcmd(0x3A); wbyte(0x66);            /* 18-bit color */
#else
    wcmd(0x3A); wbyte(0x55);            /* RGB565 */
#endif

    {
        uint8_t madctl = 0x00;
        switch (CONFIG_ROWING_DISPLAY_ROTATION) {
            case 0:   madctl = 0x00; break;
            case 90:  madctl = 0x60; break;
            case 180: madctl = 0xC0; break;
            case 270: madctl = 0xA0; break;
        }
#if CONFIG_ROWING_DISPLAY_ILI9341 || CONFIG_ROWING_DISPLAY_ILI9488
        madctl |= 0x08;                  /* BGR */
#endif
        wcmd(0x36); wbyte(madctl);
    }

#if CONFIG_ROWING_DISPLAY_GC9A01
    /* Minimal init for round 240x240 panel */
    wcmd(0xEF);
    wcmd(0xEB); wbyte(0x14);
    wcmd(0xFE);
    wcmd(0xEF);
#endif

    wcmd(0x21);
    wcmd(0x13);
    wcmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}

static esp_err_t bl_init(void)
{
    if (PIN_BL < 0) return ESP_OK;
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) return e;
    ledc_channel_config_t ch = {
        .gpio_num   = PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    e = ledc_channel_config(&ch);
    if (e == ESP_OK) s_bl_pwm_ready = true;
    return e;
}

static esp_err_t tft_set_backlight(uint8_t pct)
{
    if (PIN_BL < 0) return ESP_OK;
    if (!s_bl_pwm_ready) {
        gpio_set_level((gpio_num_t)PIN_BL, pct ? 1 : 0);
        return ESP_OK;
    }
    uint32_t duty = (uint32_t)pct * ((1U << 10) - 1) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static esp_err_t tft_init(void)
{
    /* Config control pins */
    uint64_t mask = 0;
    if (PIN_CS  >= 0) mask |= 1ULL << PIN_CS;
    if (PIN_DC  >= 0) mask |= 1ULL << PIN_DC;
    if (PIN_RST >= 0) mask |= 1ULL << PIN_RST;
    if (mask) {
        gpio_config_t io = {
            .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }
    if (PIN_BL >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << PIN_BL, .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }
    cs_high();

    /* SPI bus */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SPI_MOSI,
        .miso_io_num = BOARD_SPI_MISO,
        .sclk_io_num = BOARD_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    spi_host_device_t host = (spi_host_device_t)BOARD_SPI_HOST;
    esp_err_t e = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: 0x%x", e);
        return e;
    }
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = CONFIG_ROWING_DISPLAY_SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = -1,             /* CS handled in software */
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    e = spi_bus_add_device(host, &dev_cfg, &s_spi);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: 0x%x", e);
        return e;
    }

    init_panel();
    bl_init();
    return ESP_OK;
}

static void tft_deinit(void)
{
    if (s_spi) { spi_bus_remove_device(s_spi); s_spi = NULL; }
}

static void send_color_run(hw_color_t color, uint32_t count)
{
    uint8_t pix[PIXEL_BYTES];
#if PIXEL_BYTES == 2
    pix[0] = color >> 8;
    pix[1] = color & 0xFF;
#else
    /* Convert RGB565 to 18-bit packed (each byte top-justified) */
    pix[0] = (color >> 8) & 0xF8;
    pix[1] = (color >> 3) & 0xFC;
    pix[2] = (color << 3) & 0xF8;
#endif

    enum { CHUNK = 64 };
    uint8_t buf[CHUNK * PIXEL_BYTES];
    for (uint32_t i = 0; i < CHUNK; ++i) {
        memcpy(&buf[i * PIXEL_BYTES], pix, PIXEL_BYTES);
    }
    while (count) {
        uint32_t n = count > CHUNK ? CHUNK : count;
        spi_write(buf, n * PIXEL_BYTES, false);
        count -= n;
    }
}

static void tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, hw_color_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x >= TFT_W || y >= TFT_H) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TFT_W) w = TFT_W - x;
    if (y + h > TFT_H) h = TFT_H - y;
    if (w <= 0 || h <= 0) return;
    set_addr_window(x, y, x + w - 1, y + h - 1);
    send_color_run(color, (uint32_t)w * (uint32_t)h);
}

static void tft_pixel(int16_t x, int16_t y, hw_color_t color)
{
    tft_fill_rect(x, y, 1, 1, color);
}

static void tft_clear(hw_color_t c) { tft_fill_rect(0, 0, TFT_W, TFT_H, c); }
static void tft_flush(void)         { /* no-op for direct streaming */ }

static void draw_glyph(int16_t x, int16_t y, char ch, uint8_t scale,
                       hw_color_t fg, hw_color_t bg)
{
    if ((unsigned)ch < 0x20 || (unsigned)ch > 0x7E) ch = ' ';
    const uint8_t *glyph = font5x7[(uint8_t)ch - 0x20];
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 8; ++row) {
            hw_color_t color = (bits & (1 << row)) ? fg : bg;
            tft_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

static void tft_text(int16_t x, int16_t y, const char *s, uint8_t scale,
                     hw_color_t fg, hw_color_t bg)
{
    if (!s || scale == 0) return;
    int16_t cx = x;
    while (*s) {
        draw_glyph(cx, y, *s++, scale, fg, bg);
        cx += 6 * scale;
        if (cx >= TFT_W) break;
    }
}

static const display_driver_t s_drv = {
#if CONFIG_ROWING_DISPLAY_ST7789
    .name = "ST7789",
#elif CONFIG_ROWING_DISPLAY_ILI9341
    .name = "ILI9341",
#elif CONFIG_ROWING_DISPLAY_ILI9488
    .name = "ILI9488",
#elif CONFIG_ROWING_DISPLAY_GC9A01
    .name = "GC9A01",
#else
    .name = "ST7796",
#endif
    .width  = TFT_W,
    .height = TFT_H,
    .color  = true,
    .init           = tft_init,
    .deinit         = tft_deinit,
    .set_backlight  = tft_set_backlight,
    .clear          = tft_clear,
    .flush          = tft_flush,
    .fill_rect      = tft_fill_rect,
    .draw_pixel     = tft_pixel,
    .draw_text      = tft_text,
};

const display_driver_t *display_driver_spi_tft_get(void) { return &s_drv; }

#endif /* any TFT */
