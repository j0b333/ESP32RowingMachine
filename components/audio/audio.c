/**
 * @file audio.c
 * @brief Audio HAL dispatcher.
 */

#include "audio.h"
#include "drivers/audio_driver.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "audio";

static const audio_driver_t *s_drv = NULL;
static uint8_t s_volume = 70;
static bool    s_muted  = false;

esp_err_t audio_init(void)
{
#if !CONFIG_ROWING_AUDIO_ENABLED
    return ESP_OK;
#else
    s_volume = CONFIG_ROWING_AUDIO_DEFAULT_VOL;
    s_muted  = CONFIG_ROWING_AUDIO_MUTED_DEFAULT;
  #if CONFIG_ROWING_AUDIO_PASSIVE_BUZZER
    s_drv = audio_driver_passive_buzzer_get();
  #elif CONFIG_ROWING_AUDIO_ACTIVE_BUZZER
    s_drv = audio_driver_active_buzzer_get();
  #else
    s_drv = audio_driver_i2s_get();
  #endif
    if (!s_drv) return ESP_ERR_NOT_FOUND;
    esp_err_t e = s_drv->init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Audio driver %s init: 0x%x", s_drv->name, e);
        s_drv = NULL;
        return e;
    }
    ESP_LOGI(TAG, "Audio ready: %s", s_drv->name);
    return ESP_OK;
#endif
}

void audio_deinit(void)
{
    if (s_drv && s_drv->deinit) s_drv->deinit();
    s_drv = NULL;
}

bool      audio_is_ready(void)        { return s_drv != NULL; }
uint8_t   audio_get_volume(void)      { return s_volume; }
bool      audio_is_muted(void)        { return s_muted; }
void      audio_set_muted(bool m)     { s_muted = m; }

esp_err_t audio_set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_volume = pct;
    return ESP_OK;
}

esp_err_t audio_tone(uint16_t freq_hz, uint16_t ms)
{
    if (!s_drv || s_muted || s_volume == 0) return ESP_OK;
    return s_drv->tone(freq_hz, ms, s_volume);
}

esp_err_t audio_beep(audio_beep_t kind)
{
    if (!s_drv || s_muted || s_volume == 0) return ESP_OK;
    switch (kind) {
        case AUDIO_BEEP_TICK:     return s_drv->tone(2000, 12, s_volume);
        case AUDIO_BEEP_OK:       s_drv->tone(880, 80, s_volume);
                                  return s_drv->tone(1320, 80, s_volume);
        case AUDIO_BEEP_WARN:     s_drv->tone(660, 120, s_volume);
                                  return s_drv->tone(660, 120, s_volume);
        case AUDIO_BEEP_ERROR:    s_drv->tone(440, 200, s_volume);
                                  return s_drv->tone(330, 250, s_volume);
        case AUDIO_BEEP_STROKE:   return s_drv->tone(1500, 25, s_volume);
        case AUDIO_BEEP_INTERVAL: s_drv->tone(660, 100, s_volume);
                                  s_drv->tone(880, 100, s_volume);
                                  return s_drv->tone(1320, 200, s_volume);
        case AUDIO_BEEP_HR_ZONE:  return s_drv->tone(1000, 40, s_volume);
    }
    return ESP_OK;
}
