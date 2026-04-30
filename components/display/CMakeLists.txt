set(srcs "display.c" "display_renderer.c")

if(CONFIG_ROWING_DISPLAY_ENABLED)
    list(APPEND srcs "drivers/display_font5x7.c")
endif()
if(CONFIG_ROWING_DISPLAY_SSD1306 OR CONFIG_ROWING_DISPLAY_SH1106)
    list(APPEND srcs "drivers/display_ssd1306.c")
endif()
if(CONFIG_ROWING_DISPLAY_ST7789 OR CONFIG_ROWING_DISPLAY_ILI9341 OR
   CONFIG_ROWING_DISPLAY_ILI9488 OR CONFIG_ROWING_DISPLAY_GC9A01 OR
   CONFIG_ROWING_DISPLAY_ST7796)
    list(APPEND srcs "drivers/display_spi_tft.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "drivers"
    REQUIRES hw_hal board esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_ledc esp_timer freertos
)
