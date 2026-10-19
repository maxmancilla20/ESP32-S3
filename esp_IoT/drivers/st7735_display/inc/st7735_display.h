#ifndef ST7735_DISPLAY_H
#define ST7735_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bmp180.h"

esp_err_t st7735_display_init(void);
esp_err_t st7735_display_fill_color(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t st7735_display_show_readings(bool dht_valid, float temperature_c,
                                       float humidity, bool soil_valid,
                                       int soil_adc_raw, bool light_valid,
                                       int light_lux);
esp_err_t st7735_display_show_dht11(float temperature_c, float humidity);
esp_err_t st7735_display_show_dht11_error(void);
esp_err_t st7735_display_show_sensor_data(const bmp180_data_t *sensor);
esp_err_t st7735_display_show_sensor_error(void);

#endif
