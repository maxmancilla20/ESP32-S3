#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "esp_err.h"

esp_err_t oled_display_init(void);
esp_err_t oled_display_show_dht11(float temperature_c, float humidity);
esp_err_t oled_display_show_error(void);

#endif
