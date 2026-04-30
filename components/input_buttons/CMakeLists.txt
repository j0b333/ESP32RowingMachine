idf_component_register(
    SRCS "input_buttons.c"
    INCLUDE_DIRS "include"
    REQUIRES hw_hal esp_driver_gpio esp_timer freertos
)
