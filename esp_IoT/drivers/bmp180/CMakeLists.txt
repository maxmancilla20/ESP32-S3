idf_component_register(
    SRCS "src/bmp180.c"
    INCLUDE_DIRS "inc"
    REQUIRES i2cdev log esp_idf_lib_helpers
)
