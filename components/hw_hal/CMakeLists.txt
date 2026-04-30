idf_component_register(
    SRCS "hw_hal.c" "ui_actions.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_common
)
