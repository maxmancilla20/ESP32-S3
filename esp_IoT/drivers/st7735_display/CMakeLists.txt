idf_component_register(
    SRCS "src/st7735_display.c"
    INCLUDE_DIRS "inc"
    REQUIRES board_config bmp180 esp_driver_gpio esp_driver_spi esp_lcd esp_lcd_st7735 esp_timer
)
