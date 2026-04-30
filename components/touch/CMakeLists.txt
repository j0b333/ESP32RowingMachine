set(srcs "touch.c")
if(CONFIG_ROWING_TOUCH_XPT2046)
    list(APPEND srcs "drivers/touch_xpt2046.c")
endif()
if(CONFIG_ROWING_TOUCH_GT911 OR CONFIG_ROWING_TOUCH_FT5X06 OR CONFIG_ROWING_TOUCH_CST816S)
    list(APPEND srcs "drivers/touch_i2c.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "drivers"
    REQUIRES hw_hal board display esp_driver_gpio esp_driver_i2c esp_driver_spi freertos
)
