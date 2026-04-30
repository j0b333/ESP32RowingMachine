set(srcs "audio.c")
if(CONFIG_ROWING_AUDIO_PASSIVE_BUZZER)
    list(APPEND srcs "drivers/audio_passive_buzzer.c")
endif()
if(CONFIG_ROWING_AUDIO_ACTIVE_BUZZER)
    list(APPEND srcs "drivers/audio_active_buzzer.c")
endif()
if(CONFIG_ROWING_AUDIO_I2S_DAC OR CONFIG_ROWING_AUDIO_ES8388 OR CONFIG_ROWING_AUDIO_INTERNAL_DAC)
    list(APPEND srcs "drivers/audio_i2s.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "drivers"
    REQUIRES hw_hal board esp_driver_gpio esp_driver_ledc esp_driver_i2s esp_driver_i2c freertos
)
