/**
 * @file touch_xpt2046.c
 * @brief XPT2046 resistive touch controller (SPI). Used by ESP32-2432S028
 *        "Cheap Yellow Display" and many other ILI9341 boards.
 */

#include "sdkconfig.h"
#if CONFIG_ROWING_TOUCH_XPT2046

#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_driver.h"
#include "board.h"

static const char *TAG = "touch_xpt2046";

static spi_device_handle_t s_spi = NULL;

#define CMD_READ_X   0xD0
#define CMD_READ_Y   0x90
#define CMD_READ_Z1  0xB0
#define CMD_READ_Z2  0xC0

static uint16_t xfer_cmd(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };
    spi_transaction_t t = {
        .length    = 3 * 8,
        .rxlength  = 3 * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_spi, &t);
    return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 3) & 0x0FFF;
}

static esp_err_t xpt_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num   = BOARD_TOUCH_MOSI,
        .miso_io_num   = BOARD_TOUCH_MISO,
        .sclk_io_num   = BOARD_TOUCH_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    spi_host_device_t host = (spi_host_device_t)BOARD_TOUCH_SPI_HOST;
    esp_err_t e = spi_bus_initialize(host, &bus, SPI_DMA_DISABLED);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: 0x%x", e);
        return e;
    }
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = BOARD_TOUCH_CS,
        .queue_size = 3,
    };
    e = spi_bus_add_device(host, &cfg, &s_spi);
    if (e != ESP_OK) return e;

#if CONFIG_ROWING_TOUCH_INT_PIN >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_ROWING_TOUCH_INT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
#endif
    return ESP_OK;
}

static void xpt_deinit(void)
{
    if (s_spi) { spi_bus_remove_device(s_spi); s_spi = NULL; }
}

static bool xpt_read(int16_t *x, int16_t *y)
{
#if CONFIG_ROWING_TOUCH_INT_PIN >= 0
    if (gpio_get_level((gpio_num_t)CONFIG_ROWING_TOUCH_INT_PIN)) {
        return false;
    }
#endif
    uint16_t x1 = xfer_cmd(CMD_READ_X);
    uint16_t y1 = xfer_cmd(CMD_READ_Y);
    uint16_t x2 = xfer_cmd(CMD_READ_X);
    uint16_t y2 = xfer_cmd(CMD_READ_Y);
    uint16_t xv = (x1 + x2) / 2;
    uint16_t yv = (y1 + y2) / 2;
    if (xv < 100 || yv < 100 || xv > 4000 || yv > 4000) return false;
    *x = (int16_t)xv;
    *y = (int16_t)yv;
    return true;
}

static const touch_driver_t s_drv = {
    .name      = "XPT2046",
    .init      = xpt_init,
    .deinit    = xpt_deinit,
    .read      = xpt_read,
    .raw_max_x = 4095,
    .raw_max_y = 4095,
};

const touch_driver_t *touch_driver_xpt2046_get(void) { return &s_drv; }

#endif /* CONFIG_ROWING_TOUCH_XPT2046 */
