#ifndef ST7735_DISPLAY_H
#define ST7735_DISPLAY_H

#include "esp_err.h"
#include "bmp180.h"

esp_err_t st7735_display_init(void);
esp_err_t st7735_display_show_sensor_data(const bmp180_data_t *sensor);
esp_err_t st7735_display_show_sensor_error(void);

#endif
