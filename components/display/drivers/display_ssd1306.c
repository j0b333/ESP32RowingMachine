/**
 * @file display_ssd1306.c
 * @brief SSD1306 / SH1106 monochrome OLED driver (I2C).
 *
 * Maintains an in-RAM framebuffer (1 KB for 128x64) and pushes it on
 * flush(). SH1106 panels need a 2-pixel column offset which is applied
 * transparently when CONFIG_ROWING_DISPLAY_SH1106 is selected.
 */

#include "sdkconfig.h"

#if CONFIG_ROWING_DISPLAY_SSD1306 || CONFIG_ROWING_DISPLAY_SH1106

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display_driver.h"
#include "display_font5x7.h"
#include "board.h"

static const char *TAG = "display_oled";

#define OLED_W      128
#define OLED_H      64
#define OLED_PAGES  (OLED_H / 8)

#if CONFIG_ROWING_DISPLAY_SH1106
  #define COL_OFFSET 2
#else
  #define COL_OFFSET 0
#endif

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t                 s_fb[OLED_W * OLED_PAGES];

static esp_err_t cmd(uint8_t c)
{
    uint8_t buf[2] = {0x00, c};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t cmds(const uint8_t *list, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        esp_err_t e = cmd(list[i]);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

static esp_err_t oled_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_new_master_bus: 0x%x", err);
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_ROWING_DISPLAY_I2C_ADDR,
        .scl_speed_hz    = CONFIG_ROWING_DISPLAY_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: 0x%x", err);
        return err;
    }

    static const uint8_t init_seq[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF,
    };
    err = cmds(init_seq, sizeof init_seq);
    memset(s_fb, 0, sizeof s_fb);
    return err;
}

static void oled_deinit(void)
{
    if (s_dev) i2c_master_bus_rm_device(s_dev);
    if (s_bus) i2c_del_master_bus(s_bus);
    s_dev = NULL; s_bus = NULL;
}

static esp_err_t oled_set_backlight(uint8_t pct)
{
    /* "Backlight" on a passive OLED maps to contrast. */
    uint8_t v = (uint8_t)((uint16_t)pct * 255 / 100);
    uint8_t seq[2] = {0x81, v};
    return cmds(seq, 2);
}

static void oled_clear(hw_color_t c)
{
    memset(s_fb, c == HW_COLOR_BLACK ? 0x00 : 0xFF, sizeof s_fb);
}

static void oled_pixel(int16_t x, int16_t y, hw_color_t c)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint16_t idx = (uint16_t)((y / 8) * OLED_W + x);
    uint8_t mask = (uint8_t)(1 << (y & 7));
    if (c == HW_COLOR_BLACK) s_fb[idx] &= (uint8_t)~mask;
    else                     s_fb[idx] |= mask;
}

static void oled_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, hw_color_t c)
{
    for (int16_t yy = y; yy < y + h; ++yy)
        for (int16_t xx = x; xx < x + w; ++xx)
            oled_pixel(xx, yy, c);
}

static void draw_glyph(int16_t x, int16_t y, char ch, uint8_t scale,
                       hw_color_t fg, hw_color_t bg)
{
    if ((unsigned)ch < 0x20 || (unsigned)ch > 0x7E) ch = ' ';
    const uint8_t *glyph = font5x7[(uint8_t)ch - 0x20];
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 8; ++row) {
            hw_color_t color = (bits & (1 << row)) ? fg : bg;
            for (uint8_t sx = 0; sx < scale; ++sx)
                for (uint8_t sy = 0; sy < scale; ++sy)
                    oled_pixel(x + col * scale + sx,
                               y + row * scale + sy, color);
        }
    }
}

static void oled_text(int16_t x, int16_t y, const char *s, uint8_t scale,
                      hw_color_t fg, hw_color_t bg)
{
    if (!s || scale == 0) return;
    int16_t cx = x;
    while (*s) {
        draw_glyph(cx, y, *s++, scale, fg, bg);
        cx += 6 * scale;
        if (cx >= OLED_W) break;
    }
}

static void oled_flush(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        uint8_t addr[3] = {
            (uint8_t)(0xB0 | page),
            (uint8_t)(0x00 | (COL_OFFSET & 0x0F)),
            (uint8_t)(0x10 | ((COL_OFFSET >> 4) & 0x0F)),
        };
        if (cmds(addr, 3) != ESP_OK) return;
        uint8_t buf[OLED_W + 1];
        buf[0] = 0x40; /* data prefix */
        memcpy(buf + 1, &s_fb[page * OLED_W], OLED_W);
        if (i2c_master_transmit(s_dev, buf, OLED_W + 1, 200) != ESP_OK) return;
    }
}

static const display_driver_t s_drv = {
#if CONFIG_ROWING_DISPLAY_SH1106
    .name = "SH1106",
#else
    .name = "SSD1306",
#endif
    .width  = OLED_W,
    .height = OLED_H,
    .color  = false,
    .init           = oled_init,
    .deinit         = oled_deinit,
    .set_backlight  = oled_set_backlight,
    .clear          = oled_clear,
    .flush          = oled_flush,
    .fill_rect      = oled_fill_rect,
    .draw_pixel     = oled_pixel,
    .draw_text      = oled_text,
};

const display_driver_t *display_driver_oled_get(void) { return &s_drv; }

#endif /* SSD1306 || SH1106 */
