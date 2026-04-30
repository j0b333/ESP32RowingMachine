idf_component_register(
    SRCS "input_encoder.c"
    INCLUDE_DIRS "include"
    REQUIRES hw_hal esp_driver_pcnt esp_driver_gpio esp_timer freertos
)
