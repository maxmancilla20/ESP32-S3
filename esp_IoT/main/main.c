#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "esp_wifi.h"
//#include "esp_event.h"
//#include "esp_event_loop.h"
//#include "nvs_flash.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include <stdio.h>

#include "bms180_sensor.h"

static void app_100ms(void *arg)
{
    while (1)
    {

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void app_1000ms(void *arg)
{   
    /* Init bmp180 and i2c */
    bmp180_init_sensor();

    while (1)
    {
        bmp180_data_t sensor = {0};
        esp_err_t res = bmp180_read(&sensor);

        if (res == ESP_OK) {
            printf("T: %.2f C | P: %" PRIu32 " Pa | A: %.2f m\n",
                   sensor.temperature_c, sensor.pressure_pa, sensor.altitude_m);
        } else {
            printf("bmp180_read failed: %d\n", res);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
   // BMS180_test_task_start();
    xTaskCreate(app_100ms, "app_100ms", configMINIMAL_STACK_SIZE * 15, NULL, 5, NULL);
    xTaskCreate(app_1000ms, "app_1000ms", configMINIMAL_STACK_SIZE * 15, NULL, 5, NULL);

    //while (1)
    //{
    //    vTaskDelay(pdMS_TO_TICKS(1000));
    //}
}
