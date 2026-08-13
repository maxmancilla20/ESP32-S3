#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <stdio.h>


void app_main(void)
{
    while (1) 
    {
        printf("Hello, ESP32-S3!\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
