/**
 * @file touch_i2c.c
 * @brief Capacitive touch controllers over I2C: GT911, FT5x06/FT6236,
 *        and CST816S. A single TU because they all expose a small
 *        register window with the same conceptual layout.
 */

#include "sdkconfig.h"

#if CONFIG_ROWING_TOUCH_GT911 || CONFIG_ROWING_TOUCH_FT5X06 || CONFIG_ROWING_TOUCH_CST816S

#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "touch_driver.h"
#include "board.h"

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t reg_read(uint16_t reg, uint8_t *buf, size_t n, bool reg16)
{
    uint8_t addr[2];
    size_t alen;
    if (reg16) { addr[0] = reg >> 8; addr[1] = reg & 0xFF; alen = 2; }
    else       { addr[0] = reg & 0xFF;                     alen = 1; }
    return i2c_master_transmit_receive(s_dev, addr, alen, buf, n, 100);
}

static esp_err_t reg_write(uint16_t reg, const uint8_t *buf, size_t n, bool reg16)
{
    uint8_t tmp[16];
    if (n + (reg16 ? 2 : 1) > sizeof tmp) return ESP_ERR_INVALID_SIZE;
    size_t off = 0;
    if (reg16) { tmp[off++] = reg >> 8; tmp[off++] = reg & 0xFF; }
    else       { tmp[off++] = reg & 0xFF; }
    memcpy(tmp + off, buf, n);
    return i2c_master_transmit(s_dev, tmp, off + n, 100);
}

static esp_err_t i2c_init_common(void)
{
    if (s_bus == NULL) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = BOARD_I2C_SDA,
            .scl_io_num = BOARD_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t e = i2c_new_master_bus(&bus_cfg, &s_bus);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_ROWING_TOUCH_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
}

#if CONFIG_ROWING_TOUCH_GT911
static esp_err_t gt911_init(void)
{
    /* Optional reset/INT sequence to set address (0x5D vs 0x14) */
#if CONFIG_ROWING_TOUCH_RST_PIN >= 0 && CONFIG_ROWING_TOUCH_INT_PIN >= 0
    gpio_set_direction((gpio_num_t)CONFIG_ROWING_TOUCH_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)CONFIG_ROWING_TOUCH_INT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)CONFIG_ROWING_TOUCH_RST_PIN, 0);
    gpio_set_level((gpio_num_t)CONFIG_ROWING_TOUCH_INT_PIN,
                   CONFIG_ROWING_TOUCH_I2C_ADDR == 0x14 ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level((gpio_num_t)CONFIG_ROWING_TOUCH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction((gpio_num_t)CONFIG_ROWING_TOUCH_INT_PIN, GPIO_MODE_INPUT);
#endif
    return i2c_init_common();
}

static bool gt911_read(int16_t *x, int16_t *y)
{
    uint8_t status = 0;
    if (reg_read(0x814E, &status, 1, true) != ESP_OK) return false;
    bool buf_ready = status & 0x80;
    uint8_t pts = status & 0x0F;
    if (!buf_ready || pts == 0) return false;

    uint8_t pt[6];
    if (reg_read(0x8150, pt, sizeof pt, true) != ESP_OK) return false;
    *x = (int16_t)((uint16_t)pt[0] | ((uint16_t)pt[1] << 8));
    *y = (int16_t)((uint16_t)pt[2] | ((uint16_t)pt[3] << 8));

    /* Clear status register so next sample arrives */
    uint8_t zero = 0;
    reg_write(0x814E, &zero, 1, true);
    return true;
}
#endif /* GT911 */

#if CONFIG_ROWING_TOUCH_FT5X06
static esp_err_t ft_init(void) { return i2c_init_common(); }

static bool ft_read(int16_t *x, int16_t *y)
{
    uint8_t buf[7];
    if (reg_read(0x00, buf, 7, false) != ESP_OK) return false;
    uint8_t pts = buf[2] & 0x0F;
    if (pts == 0) return false;
    *x = (int16_t)((((uint16_t)buf[3] & 0x0F) << 8) | buf[4]);
    *y = (int16_t)((((uint16_t)buf[5] & 0x0F) << 8) | buf[6]);
    return true;
}
#endif /* FT5X06 */

#if CONFIG_ROWING_TOUCH_CST816S
static esp_err_t cst_init(void)
{
#if CONFIG_ROWING_TOUCH_RST_PIN >= 0
    gpio_set_direction((gpio_num_t)CONFIG_ROWING_TOUCH_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)CONFIG_ROWING_TOUCH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)CONFIG_ROWING_TOUCH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
    return i2c_init_common();
}

static bool cst_read(int16_t *x, int16_t *y)
{
    uint8_t buf[7];
    /* CST816S layout: 0x01 GestureID, 0x02 FingerNum, 0x03..0x06 XYxy */
    if (reg_read(0x01, buf, 6, false) != ESP_OK) return false;
    if (buf[1] == 0) return false;
    *x = (int16_t)((((uint16_t)buf[2] & 0x0F) << 8) | buf[3]);
    *y = (int16_t)((((uint16_t)buf[4] & 0x0F) << 8) | buf[5]);
    return true;
}
#endif /* CST816S */

static const touch_driver_t s_drv = {
#if CONFIG_ROWING_TOUCH_GT911
    .name = "GT911",
    .init = gt911_init,
    .read = gt911_read,
    .raw_max_x = 800,
    .raw_max_y = 480,
#elif CONFIG_ROWING_TOUCH_FT5X06
    .name = "FT5x06",
    .init = ft_init,
    .read = ft_read,
    .raw_max_x = 480,
    .raw_max_y = 320,
#else
    .name = "CST816S",
    .init = cst_init,
    .read = cst_read,
    .raw_max_x = 240,
    .raw_max_y = 240,
#endif
    .deinit = NULL,
};

const touch_driver_t *touch_driver_i2c_get(void) { return &s_drv; }

#endif /* any I2C cap touch */
