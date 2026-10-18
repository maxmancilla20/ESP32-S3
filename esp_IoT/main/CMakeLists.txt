idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bmp180 buzzer dht ir_nec oled_display rgb_led st7735_display)
