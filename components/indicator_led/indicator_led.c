/**
 * @file indicator_led.c
 * @brief Status LED HAL — runtime polymorphism over the configured backend.
 */

#include "indicator_led.h"

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "sdkconfig.h"
#include "board.h"

#if CONFIG_ROWING_LED_WS2812
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#endif

static const char *TAG = "led";

#if CONFIG_ROWING_LED_ENABLED

static led_state_t s_state = LED_STATE_OFF;
static TaskHandle_t s_task = NULL;
static volatile bool s_run = false;

static int pin_main(void)
{
#if CONFIG_ROWING_LED_PIN >= 0
    return CONFIG_ROWING_LED_PIN;
#else
    return BOARD_RGB_LED_PIN;
#endif
}

/* ----------------- WS2812 backend ------------------------------------ */
#if CONFIG_ROWING_LED_WS2812
static rmt_channel_handle_t  s_rmt = NULL;
static rmt_encoder_handle_t  s_enc = NULL;

static esp_err_t ws_init(void)
{
    int pin = pin_main();
    if (pin < 0) return ESP_ERR_INVALID_ARG;
    rmt_tx_channel_config_t cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = pin,
        .mem_block_symbols = 64,
        .resolution_hz = 10 * 1000 * 1000,    /* 10 MHz, 100 ns ticks */
        .trans_queue_depth = 2,
    };
    esp_err_t e = rmt_new_tx_channel(&cfg, &s_rmt);
    if (e != ESP_OK) return e;
    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    e = rmt_new_bytes_encoder(&enc_cfg, &s_enc);
    if (e != ESP_OK) return e;
    return rmt_enable(s_rmt);
}

static esp_err_t ws_show_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_rmt || !s_enc) return ESP_OK;
    /* WS2812 wants GRB order */
    uint8_t pix[3] = { g, r, b };
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    return rmt_transmit(s_rmt, s_enc, pix, sizeof pix, &tx_cfg);
}
#endif

/* ----------------- RGB PWM backend ----------------------------------- */
#if CONFIG_ROWING_LED_RGB_PWM
static esp_err_t pwm_setup_channel(int gpio, ledc_channel_t ch)
{
    ledc_channel_config_t c = {
        .gpio_num = gpio, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = ch, .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_2, .duty = 0, .hpoint = 0,
    };
    return ledc_channel_config(&c);
}
static esp_err_t rgbpwm_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) return e;
    if (CONFIG_ROWING_LED_R_PIN >= 0) pwm_setup_channel(CONFIG_ROWING_LED_R_PIN, LEDC_CHANNEL_3);
    if (CONFIG_ROWING_LED_G_PIN >= 0) pwm_setup_channel(CONFIG_ROWING_LED_G_PIN, LEDC_CHANNEL_4);
    if (CONFIG_ROWING_LED_B_PIN >= 0) pwm_setup_channel(CONFIG_ROWING_LED_B_PIN, LEDC_CHANNEL_5);
    return ESP_OK;
}
static esp_err_t rgbpwm_show(uint8_t r, uint8_t g, uint8_t b)
{
    if (CONFIG_ROWING_LED_R_PIN >= 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, r);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
    }
    if (CONFIG_ROWING_LED_G_PIN >= 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, g);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4);
    }
    if (CONFIG_ROWING_LED_B_PIN >= 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, b);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5);
    }
    return ESP_OK;
}
#endif

/* ----------------- PWM mono backend ---------------------------------- */
#if CONFIG_ROWING_LED_PWM
static esp_err_t pwm_init(void)
{
    int p = pin_main();
    if (p < 0) return ESP_ERR_INVALID_ARG;
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_3,
        .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = p, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_6, .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_3, .duty = 0, .hpoint = 0,
    };
    return ledc_channel_config(&c);
}
static esp_err_t pwm_set(uint8_t v)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6, v);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6);
}
#endif

/* ----------------- Plain GPIO backend -------------------------------- */
#if CONFIG_ROWING_LED_PLAIN
static esp_err_t plain_init(void)
{
    int p = pin_main();
    if (p < 0) return ESP_ERR_INVALID_ARG;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << p, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}
static void plain_set(bool on)
{
    int p = pin_main();
    if (p < 0) return;
#if CONFIG_ROWING_LED_ACTIVE_LOW
    gpio_set_level((gpio_num_t)p, on ? 0 : 1);
#else
    gpio_set_level((gpio_num_t)p, on ? 1 : 0);
#endif
}
#endif

/* ----------------- common output ------------------------------------- */

static void show_rgb(uint8_t r, uint8_t g, uint8_t b)
{
#if CONFIG_ROWING_LED_WS2812
    ws_show_rgb(r, g, b);
#elif CONFIG_ROWING_LED_RGB_PWM
    rgbpwm_show(r, g, b);
#elif CONFIG_ROWING_LED_PWM
    /* Convert to luminance */
    uint16_t y = (299u * r + 587u * g + 114u * b) / 1000u;
    pwm_set((uint8_t)y);
#elif CONFIG_ROWING_LED_PLAIN
    plain_set(r || g || b);
#else
    (void)r; (void)g; (void)b;
#endif
}

static void render_state(led_state_t s, uint32_t tick_ms)
{
    bool blink_slow = ((tick_ms / 500) & 1);
    bool blink_fast = ((tick_ms / 120) & 1);
    uint8_t breathe = (uint8_t)((tick_ms / 8) % 256);
    if (breathe > 127) breathe = (uint8_t)(255 - breathe);
    breathe = (uint8_t)(breathe * 2);

    switch (s) {
        case LED_STATE_OFF:               show_rgb(0,0,0); break;
        case LED_STATE_BOOT:              show_rgb(blink_slow?40:0, blink_slow?20:0, 0); break;
        case LED_STATE_IDLE:              show_rgb(8,8,8); break;
        case LED_STATE_ACTIVE:            show_rgb(0,80,0); break;
        case LED_STATE_PAUSED:            show_rgb(breathe, breathe/2, 0); break;
        case LED_STATE_BLE_ADVERTISING:   show_rgb(0,0, blink_slow?60:5); break;
        case LED_STATE_WIFI_PROVISIONING: show_rgb(0,0, blink_fast?80:0); break;
        case LED_STATE_HR_CONNECTED:      show_rgb(80,0,0); break;
        case LED_STATE_ERROR:             show_rgb(blink_fast?120:0, 0, 0); break;
    }
}

static void led_task(void *arg)
{
    (void)arg;
    uint32_t t = 0;
    while (s_run) {
        render_state(s_state, t);
        vTaskDelay(pdMS_TO_TICKS(50));
        t += 50;
    }
    show_rgb(0,0,0);
    vTaskDelete(NULL);
}

#endif /* CONFIG_ROWING_LED_ENABLED */

esp_err_t indicator_led_init(void)
{
#if !CONFIG_ROWING_LED_ENABLED
    return ESP_OK;
#else
    esp_err_t e = ESP_OK;
  #if CONFIG_ROWING_LED_WS2812
    e = ws_init();
  #elif CONFIG_ROWING_LED_RGB_PWM
    e = rgbpwm_init();
  #elif CONFIG_ROWING_LED_PWM
    e = pwm_init();
  #elif CONFIG_ROWING_LED_PLAIN
    e = plain_init();
  #endif
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "LED init: 0x%x", e);
        return e;
    }
    s_state = LED_STATE_BOOT;
    s_run = true;
    if (xTaskCreate(led_task, "led", 2048, NULL, 2, &s_task) != pdPASS) {
        s_run = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#endif
}

esp_err_t indicator_led_set(led_state_t s)
{
#if CONFIG_ROWING_LED_ENABLED
    s_state = s;
#else
    (void)s;
#endif
    return ESP_OK;
}

esp_err_t indicator_led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
#if CONFIG_ROWING_LED_ENABLED
    show_rgb(r, g, b);
#else
    (void)r; (void)g; (void)b;
#endif
    return ESP_OK;
}

void indicator_led_deinit(void)
{
#if CONFIG_ROWING_LED_ENABLED
    s_run = false;
    s_task = NULL;
#endif
}
