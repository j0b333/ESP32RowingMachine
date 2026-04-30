/**
 * @file indicator_led.h
 * @brief Status LED HAL — covers plain GPIO, PWM, RGB-PWM and WS2812.
 *
 * The application calls a high-level "state" API; the driver maps it
 * into a colour / blink pattern appropriate for the configured LED.
 */
#ifndef INDICATOR_LED_H
#define INDICATOR_LED_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_BOOT,             /* slow yellow / single blink */
    LED_STATE_IDLE,              /* dim white / off  */
    LED_STATE_ACTIVE,            /* solid green */
    LED_STATE_PAUSED,            /* breathing yellow */
    LED_STATE_BLE_ADVERTISING,   /* slow blue */
    LED_STATE_WIFI_PROVISIONING, /* fast blue */
    LED_STATE_HR_CONNECTED,      /* solid red */
    LED_STATE_ERROR,             /* fast red blink */
} led_state_t;

esp_err_t indicator_led_init(void);
void      indicator_led_deinit(void);
esp_err_t indicator_led_set(led_state_t s);

/** Direct RGB override (use 0x000000 to revert to last `set` state). */
esp_err_t indicator_led_rgb(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif
