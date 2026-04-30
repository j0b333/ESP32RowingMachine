/**
 * @file audio_i2s.c
 * @brief I2S audio backends.
 *
 * Generates a square-wave tone at the requested frequency and streams
 * it to the I2S peripheral. Works with either:
 *
 *   - A simple I2S DAC such as MAX98357A or PCM5102 (`ROWING_AUDIO_I2S_DAC`)
 *   - A codec such as ES8388 driven via the same I2S pins; the codec
 *     itself is not configured here — for full codec control include a
 *     dedicated codec driver. We at least open the I2S port so beeps
 *     come out.
 *   - The ESP32 internal DAC via the legacy I2S DAC mode
 *     (`ROWING_AUDIO_INTERNAL_DAC`, ESP32 only).
 *
 * For most rowing-monitor use cases (start beeps, interval chimes,
 * stroke ticks) a square-wave tone generator is sufficient and avoids
 * shipping any real audio assets.
 */

#include "sdkconfig.h"
#if CONFIG_ROWING_AUDIO_I2S_DAC || CONFIG_ROWING_AUDIO_ES8388 || CONFIG_ROWING_AUDIO_INTERNAL_DAC

#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "audio_driver.h"
#include "board.h"

/* 32 kHz keeps even high tones (~6 kHz) well above the Nyquist limit
 * while remaining cheap enough for square-wave generation on the CPU.
 * Beep audio quality requirements are minimal, so a higher rate would
 * be wasted bandwidth. */
#define SAMPLE_RATE 32000

static i2s_chan_handle_t s_tx = NULL;

static esp_err_t init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t e = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (e != ESP_OK) return e;

    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_I2S_BCLK,
            .ws   = BOARD_I2S_LRCLK,
            .dout = BOARD_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { 0, 0, 0 },
        },
    };
    e = i2s_channel_init_std_mode(s_tx, &cfg);
    if (e != ESP_OK) return e;
    return i2s_channel_enable(s_tx);
}

static void deinit(void)
{
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
}

static esp_err_t tone(uint16_t freq, uint16_t ms, uint8_t volume)
{
    if (!s_tx || ms == 0) return ESP_OK;
    if (freq < 30) freq = 30;
    if (freq > 8000) freq = 8000;

    uint32_t total_samples = (uint32_t)SAMPLE_RATE * ms / 1000;
    uint32_t half_period   = SAMPLE_RATE / (2u * freq);
    if (half_period == 0) half_period = 1;

    int16_t amp = (int16_t)((INT16_MAX / 2) * volume / 100);

    enum { CHUNK = 256 };
    int16_t buf[CHUNK];
    uint32_t phase = 0;
    bool     hi    = true;

    while (total_samples) {
        uint32_t n = total_samples > CHUNK ? CHUNK : total_samples;
        for (uint32_t i = 0; i < n; ++i) {
            buf[i] = hi ? amp : (int16_t)-amp;
            if (++phase >= half_period) { phase = 0; hi = !hi; }
        }
        size_t written = 0;
        i2s_channel_write(s_tx, buf, n * sizeof(int16_t), &written, 100);
        total_samples -= n;
    }
    return ESP_OK;
}

static const audio_driver_t s_drv = {
#if CONFIG_ROWING_AUDIO_ES8388
    .name = "I2S+ES8388",
#elif CONFIG_ROWING_AUDIO_INTERNAL_DAC
    .name = "I2S-internal-DAC",
#else
    .name = "I2S-DAC",
#endif
    .supports_freq = true,
    .init = init, .deinit = deinit, .tone = tone,
};

const audio_driver_t *audio_driver_i2s_get(void) { return &s_drv; }

#endif
