idf_component_register(
    SRCS "indicator_led.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_gpio esp_driver_ledc esp_driver_rmt freertos
)
