/**
 * @file board.h
 * @brief Per-board pin map.
 *
 * Each supported board defines a set of `BOARD_*` macros that the
 * display / touch / audio / input components use as defaults. Boards
 * that don't define a particular pin fall back to -1 (unused).
 *
 * The active board is selected by the `ROWING_BOARD_*` choice in
 * `components/board/Kconfig`.
 */

#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*                       BOARD DEFAULT PIN MAP                         */
/* ------------------------------------------------------------------ */

#if CONFIG_ROWING_BOARD_DEVKITC_S3

    #define BOARD_NAME              "ESP32-S3 DevKitC-1"
    #define BOARD_FLYWHEEL_PIN      15
    #define BOARD_SEAT_PIN          16
    #define BOARD_I2C_SDA           21
    #define BOARD_I2C_SCL           22
    #define BOARD_BTN1_PIN          0
    #define BOARD_BTN2_PIN          -1
    #define BOARD_RGB_LED_PIN       48
    #define BOARD_LED_IS_WS2812     1
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          11
    #define BOARD_SPI_MISO          13
    #define BOARD_SPI_SCK           12

#elif CONFIG_ROWING_BOARD_DEVKITC_ESP32

    #define BOARD_NAME              "ESP32 DevKitC"
    #define BOARD_FLYWHEEL_PIN      15
    #define BOARD_SEAT_PIN          16
    #define BOARD_I2C_SDA           21
    #define BOARD_I2C_SCL           22
    #define BOARD_BTN1_PIN          0
    #define BOARD_BTN2_PIN          -1
    #define BOARD_RGB_LED_PIN       2
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          23
    #define BOARD_SPI_MISO          19
    #define BOARD_SPI_SCK           18

#elif CONFIG_ROWING_BOARD_LILYGO_TDISPLAY_S3

    #define BOARD_NAME              "LilyGO T-Display-S3"
    #define BOARD_FLYWHEEL_PIN      17
    #define BOARD_SEAT_PIN          18
    #define BOARD_I2C_SDA           43
    #define BOARD_I2C_SCL           44
    #define BOARD_BTN1_PIN          0
    #define BOARD_BTN2_PIN          14
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_DISPLAY_DC        7
    #define BOARD_DISPLAY_RST       5
    #define BOARD_DISPLAY_BL        38
    #define BOARD_DISPLAY_CS        6

#elif CONFIG_ROWING_BOARD_LILYGO_TDISPLAY

    #define BOARD_NAME              "LilyGO T-Display"
    #define BOARD_FLYWHEEL_PIN      15
    #define BOARD_SEAT_PIN          13
    #define BOARD_I2C_SDA           21
    #define BOARD_I2C_SCL           22
    #define BOARD_BTN1_PIN          35
    #define BOARD_BTN2_PIN          0
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          19
    #define BOARD_SPI_MISO          -1
    #define BOARD_SPI_SCK           18
    #define BOARD_DISPLAY_CS        5
    #define BOARD_DISPLAY_DC        16
    #define BOARD_DISPLAY_RST       23
    #define BOARD_DISPLAY_BL        4

#elif CONFIG_ROWING_BOARD_CYD_2432S028

    #define BOARD_NAME              "ESP32-2432S028 (CYD)"
    #define BOARD_FLYWHEEL_PIN      35
    #define BOARD_SEAT_PIN          22
    #define BOARD_I2C_SDA           27
    #define BOARD_I2C_SCL           26
    #define BOARD_BTN1_PIN          0
    #define BOARD_BTN2_PIN          -1
    #define BOARD_RGB_LED_PIN       4
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          13
    #define BOARD_SPI_MISO          12
    #define BOARD_SPI_SCK           14
    #define BOARD_DISPLAY_CS        15
    #define BOARD_DISPLAY_DC        2
    #define BOARD_DISPLAY_RST       -1
    #define BOARD_DISPLAY_BL        21
    #define BOARD_TOUCH_SPI_HOST    2
    #define BOARD_TOUCH_MOSI        32
    #define BOARD_TOUCH_MISO        39
    #define BOARD_TOUCH_SCK         25
    #define BOARD_TOUCH_CS          33
    #define BOARD_TOUCH_IRQ         36

#elif CONFIG_ROWING_BOARD_WT32_SC01

    #define BOARD_NAME              "WT32-SC01"
    #define BOARD_FLYWHEEL_PIN      27
    #define BOARD_SEAT_PIN          26
    #define BOARD_I2C_SDA           18
    #define BOARD_I2C_SCL           19
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          13
    #define BOARD_SPI_MISO          -1
    #define BOARD_SPI_SCK           14
    #define BOARD_DISPLAY_CS        15
    #define BOARD_DISPLAY_DC        21
    #define BOARD_DISPLAY_RST       22
    #define BOARD_DISPLAY_BL        23
    #define BOARD_TOUCH_INT         39

#elif CONFIG_ROWING_BOARD_WT32_SC01_PLUS

    #define BOARD_NAME              "WT32-SC01-Plus"
    #define BOARD_FLYWHEEL_PIN      10
    #define BOARD_SEAT_PIN          11
    #define BOARD_I2C_SDA           6
    #define BOARD_I2C_SCL           5
    #define BOARD_BTN1_PIN          0
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_DISPLAY_BL        45
    #define BOARD_DISPLAY_RST       4
    #define BOARD_DISPLAY_DC        0
    #define BOARD_TOUCH_RST         4
    #define BOARD_TOUCH_INT         7

#elif CONFIG_ROWING_BOARD_M5STACK_CORE2

    #define BOARD_NAME              "M5Stack Core2"
    #define BOARD_FLYWHEEL_PIN      36
    #define BOARD_SEAT_PIN          26
    #define BOARD_I2C_SDA           21
    #define BOARD_I2C_SCL           22
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          23
    #define BOARD_SPI_MISO          38
    #define BOARD_SPI_SCK           18
    #define BOARD_DISPLAY_CS        5
    #define BOARD_DISPLAY_DC        15
    #define BOARD_TOUCH_INT         39
    #define BOARD_I2S_BCLK          12
    #define BOARD_I2S_LRCLK         0
    #define BOARD_I2S_DOUT          2
    #define BOARD_I2S_DIN           34

#elif CONFIG_ROWING_BOARD_WAVESHARE_LCD128

    #define BOARD_NAME              "Waveshare ESP32-S3 Touch LCD 1.28"
    #define BOARD_FLYWHEEL_PIN      9
    #define BOARD_SEAT_PIN          10
    #define BOARD_I2C_SDA           6
    #define BOARD_I2C_SCL           7
    #define BOARD_BTN1_PIN          0
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          11
    #define BOARD_SPI_MISO          -1
    #define BOARD_SPI_SCK           10
    #define BOARD_DISPLAY_CS        9
    #define BOARD_DISPLAY_DC        8
    #define BOARD_DISPLAY_RST       14
    #define BOARD_DISPLAY_BL        2
    #define BOARD_TOUCH_INT         5
    #define BOARD_TOUCH_RST         13

#elif CONFIG_ROWING_BOARD_ESP_BOX

    #define BOARD_NAME              "Espressif ESP-Box"
    #define BOARD_FLYWHEEL_PIN      10
    #define BOARD_SEAT_PIN          11
    #define BOARD_I2C_SDA           8
    #define BOARD_I2C_SCL           18
    #define BOARD_BTN1_PIN          0
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0
    #define BOARD_SPI_HOST          1
    #define BOARD_SPI_MOSI          6
    #define BOARD_SPI_MISO          -1
    #define BOARD_SPI_SCK           7
    #define BOARD_DISPLAY_CS        5
    #define BOARD_DISPLAY_DC        4
    #define BOARD_DISPLAY_RST       48
    #define BOARD_DISPLAY_BL        47
    #define BOARD_TOUCH_INT         3
    #define BOARD_I2S_BCLK          17
    #define BOARD_I2S_LRCLK         47
    #define BOARD_I2S_DOUT          15
    #define BOARD_I2S_DIN           16

#else /* HEADLESS / custom */

    #define BOARD_NAME              "Headless / custom"
    #define BOARD_FLYWHEEL_PIN      15
    #define BOARD_SEAT_PIN          16
    #define BOARD_I2C_SDA           21
    #define BOARD_I2C_SCL           22
    #define BOARD_BTN1_PIN          -1
    #define BOARD_BTN2_PIN          -1
    #define BOARD_RGB_LED_PIN       -1
    #define BOARD_LED_IS_WS2812     0

#endif

/* Defaulted pin macros for any board that didn't specify them. */
#ifndef BOARD_FLYWHEEL_PIN
#define BOARD_FLYWHEEL_PIN  15
#endif
#ifndef BOARD_SEAT_PIN
#define BOARD_SEAT_PIN      16
#endif
#ifndef BOARD_I2C_SDA
#define BOARD_I2C_SDA       21
#endif
#ifndef BOARD_I2C_SCL
#define BOARD_I2C_SCL       22
#endif
#ifndef BOARD_BTN1_PIN
#define BOARD_BTN1_PIN      -1
#endif
#ifndef BOARD_BTN2_PIN
#define BOARD_BTN2_PIN      -1
#endif
#ifndef BOARD_RGB_LED_PIN
#define BOARD_RGB_LED_PIN   -1
#endif
#ifndef BOARD_LED_IS_WS2812
#define BOARD_LED_IS_WS2812 0
#endif
#ifndef BOARD_SPI_HOST
#define BOARD_SPI_HOST      1
#endif
#ifndef BOARD_SPI_MOSI
#define BOARD_SPI_MOSI      -1
#endif
#ifndef BOARD_SPI_MISO
#define BOARD_SPI_MISO      -1
#endif
#ifndef BOARD_SPI_SCK
#define BOARD_SPI_SCK       -1
#endif
#ifndef BOARD_DISPLAY_CS
#define BOARD_DISPLAY_CS    -1
#endif
#ifndef BOARD_DISPLAY_DC
#define BOARD_DISPLAY_DC    -1
#endif
#ifndef BOARD_DISPLAY_RST
#define BOARD_DISPLAY_RST   -1
#endif
#ifndef BOARD_DISPLAY_BL
#define BOARD_DISPLAY_BL    -1
#endif
#ifndef BOARD_TOUCH_SPI_HOST
#define BOARD_TOUCH_SPI_HOST    BOARD_SPI_HOST
#endif
#ifndef BOARD_TOUCH_MOSI
#define BOARD_TOUCH_MOSI    BOARD_SPI_MOSI
#endif
#ifndef BOARD_TOUCH_MISO
#define BOARD_TOUCH_MISO    BOARD_SPI_MISO
#endif
#ifndef BOARD_TOUCH_SCK
#define BOARD_TOUCH_SCK     BOARD_SPI_SCK
#endif
#ifndef BOARD_TOUCH_CS
#define BOARD_TOUCH_CS      -1
#endif
#ifndef BOARD_TOUCH_IRQ
#define BOARD_TOUCH_IRQ     -1
#endif
#ifndef BOARD_TOUCH_INT
#define BOARD_TOUCH_INT     -1
#endif
#ifndef BOARD_TOUCH_RST
#define BOARD_TOUCH_RST     -1
#endif
#ifndef BOARD_I2S_BCLK
#define BOARD_I2S_BCLK      -1
#endif
#ifndef BOARD_I2S_LRCLK
#define BOARD_I2S_LRCLK     -1
#endif
#ifndef BOARD_I2S_DOUT
#define BOARD_I2S_DOUT      -1
#endif
#ifndef BOARD_I2S_DIN
#define BOARD_I2S_DIN       -1
#endif

const char *board_get_name(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
