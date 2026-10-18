idf_component_register(
    SRCS "src/ir_nec_encoder.c" "src/ir_nec_transceiver.c"
    INCLUDE_DIRS "inc"
    REQUIRES esp_driver_gpio esp_driver_rmt esp_timer rgb_led
)
