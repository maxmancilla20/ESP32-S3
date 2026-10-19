idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES board_config dht esp_adc st7735_display)
