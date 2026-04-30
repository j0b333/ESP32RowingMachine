/**
 * @file audio_passive_buzzer.c
 * @brief LEDC PWM tone generator for passive piezo buzzers.
 */

#include "sdkconfig.h"
#if CONFIG_ROWING_AUDIO_PASSIVE_BUZZER

#include "esp_err.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_driver.h"
#include "board.h"

#define BUZZ_LEDC_TIMER     LEDC_TIMER_1
#define BUZZ_LEDC_CHANNEL   LEDC_CHANNEL_1
#define BUZZ_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BUZZ_RESOLUTION     LEDC_TIMER_10_BIT

static int pin(void)
{
#if CONFIG_ROWING_AUDIO_PIN >= 0
    return CONFIG_ROWING_AUDIO_PIN;
#else
    return 25;  /* generic default */
#endif
}

static esp_err_t init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = BUZZ_LEDC_MODE,
        .timer_num       = BUZZ_LEDC_TIMER,
        .duty_resolution = BUZZ_RESOLUTION,
        .freq_hz         = 2000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) return e;
    ledc_channel_config_t ch = {
        .gpio_num   = pin(),
        .speed_mode = BUZZ_LEDC_MODE,
        .channel    = BUZZ_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BUZZ_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch);
}

static void deinit(void)
{
    ledc_stop(BUZZ_LEDC_MODE, BUZZ_LEDC_CHANNEL, 0);
}

static esp_err_t tone(uint16_t freq, uint16_t ms, uint8_t volume)
{
    if (freq < 30) freq = 30;
    if (freq > 12000) freq = 12000;
    ledc_set_freq(BUZZ_LEDC_MODE, BUZZ_LEDC_TIMER, freq);
    /* 50% duty = max amplitude. Scale by volume. */
    uint32_t max = (1U << 10) - 1;
    uint32_t duty = (max / 2) * volume / 100;
    ledc_set_duty(BUZZ_LEDC_MODE, BUZZ_LEDC_CHANNEL, duty);
    ledc_update_duty(BUZZ_LEDC_MODE, BUZZ_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(BUZZ_LEDC_MODE, BUZZ_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZ_LEDC_MODE, BUZZ_LEDC_CHANNEL);
    return ESP_OK;
}

static const audio_driver_t s_drv = {
    .name = "passive-buzzer",
    .supports_freq = true,
    .init = init, .deinit = deinit, .tone = tone,
};

const audio_driver_t *audio_driver_passive_buzzer_get(void) { return &s_drv; }

#endif
