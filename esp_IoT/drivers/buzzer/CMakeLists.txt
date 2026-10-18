idf_component_register(
    SRCS "src/buzzer.c"
    INCLUDE_DIRS "inc"
    REQUIRES esp_driver_gpio esp_driver_ledc
)
