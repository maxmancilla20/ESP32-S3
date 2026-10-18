idf_component_register(
    SRCS "src/oled_display.c"
    INCLUDE_DIRS "inc"
    REQUIRES esp_driver_gpio i2cdev
)
