/**
 * @file audio.h
 * @brief Audio HAL — uniform tone / beep API across all backends.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_BEEP_TICK = 0,        /* very short click */
    AUDIO_BEEP_OK,              /* affirmative chime */
    AUDIO_BEEP_WARN,            /* attention */
    AUDIO_BEEP_ERROR,           /* error tone */
    AUDIO_BEEP_STROKE,          /* per-stroke marker */
    AUDIO_BEEP_INTERVAL,        /* interval boundary */
    AUDIO_BEEP_HR_ZONE,         /* heart-rate zone change */
} audio_beep_t;

esp_err_t audio_init(void);
void      audio_deinit(void);

bool      audio_is_ready(void);

esp_err_t audio_set_volume(uint8_t pct);     /* 0..100 */
uint8_t   audio_get_volume(void);

void      audio_set_muted(bool muted);
bool      audio_is_muted(void);

/** Play a single sine-ish tone (or click) for `ms` milliseconds. */
esp_err_t audio_tone(uint16_t freq_hz, uint16_t ms);

/** Play one of the canned beep effects. */
esp_err_t audio_beep(audio_beep_t kind);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
