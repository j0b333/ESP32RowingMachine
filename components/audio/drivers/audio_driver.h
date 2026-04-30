/**
 * @file audio_driver.h
 * @brief Internal vtable for audio backends.
 */
#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *name;
    bool        supports_freq;
    esp_err_t (*init)(void);
    void      (*deinit)(void);
    esp_err_t (*tone)(uint16_t freq_hz, uint16_t ms, uint8_t volume);
} audio_driver_t;

#if CONFIG_ROWING_AUDIO_PASSIVE_BUZZER
const audio_driver_t *audio_driver_passive_buzzer_get(void);
#endif
#if CONFIG_ROWING_AUDIO_ACTIVE_BUZZER
const audio_driver_t *audio_driver_active_buzzer_get(void);
#endif
#if CONFIG_ROWING_AUDIO_I2S_DAC || CONFIG_ROWING_AUDIO_ES8388 || CONFIG_ROWING_AUDIO_INTERNAL_DAC
const audio_driver_t *audio_driver_i2s_get(void);
#endif

#endif
