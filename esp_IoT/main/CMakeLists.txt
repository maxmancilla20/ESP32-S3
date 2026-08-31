idf_component_register(SRCS "main.c" "bms180_sensor.c"
                    INCLUDE_DIRS "."
                    REQUIRES bmp180 i2cdev esp_idf_lib_helpers)

