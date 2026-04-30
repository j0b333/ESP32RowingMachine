/**
 * @file audio_active_buzzer.c
 * @brief Active buzzer (ignores frequency, just pulses GPIO).
 */
#include "sdkconfig.h"
#if CONFIG_ROWING_AUDIO_ACTIVE_BUZZER

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_driver.h"

static int pin(void)
{
#if CONFIG_ROWING_AUDIO_PIN >= 0
    return CONFIG_ROWING_AUDIO_PIN;
#else
    return 25;
#endif
}

static esp_err_t init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin(),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0, .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level((gpio_num_t)pin(), 0);
    return gpio_config(&io);
}

static void deinit(void)
{
    gpio_set_level((gpio_num_t)pin(), 0);
}

static esp_err_t tone(uint16_t freq, uint16_t ms, uint8_t volume)
{
    (void)freq;
    if (volume == 0) return ESP_OK;
    gpio_set_level((gpio_num_t)pin(), 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level((gpio_num_t)pin(), 0);
    return ESP_OK;
}

static const audio_driver_t s_drv = {
    .name = "active-buzzer",
    .supports_freq = false,
    .init = init, .deinit = deinit, .tone = tone,
};
const audio_driver_t *audio_driver_active_buzzer_get(void) { return &s_drv; }
#endif
