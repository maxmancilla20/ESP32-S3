#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include "esp_log.h"
#include "rgb_led.h"
#include "ir_nec_transceiver.h"
#include "bmp180.h"
#include "st7735_display.h"
#include "dht.h"
#include "oled_display.h"
#include "buzzer.h"

static const char *TAG = "lcd";
#define DHT11_GPIO GPIO_NUM_6
#define SENSOR_TASK_PERIOD_MS 1000

static void app_100ms(void *arg)
{
    init_led_color();
    while (1)
    {
        //set_led_color(128, 0, 128);
        vTaskDelay(pdMS_TO_TICKS(500));
        set_led_color(1, 10, 20);
        vTaskDelay(pdMS_TO_TICKS(500));
       //set_led_color(0, 0, 255);
       //vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void app_1000ms(void *arg)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1)
    {
        bmp180_data_t sensor = {0};
        esp_err_t res = bmp180_read(&sensor);
        if (res == ESP_OK) {
            //printf("T: %.2f C | P: %" PRIu32 " Pa | A: %.2f m\n",
            //       sensor.temperature_c, sensor.pressure_pa, sensor.altitude_m);
            ESP_ERROR_CHECK(st7735_display_show_sensor_data(&sensor));
        } else {
            ESP_LOGE(TAG, "BMP180 read failed: %s", esp_err_to_name(res));
            ESP_ERROR_CHECK(st7735_display_show_sensor_error());
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

static void dht11_oled_task(void *arg)
{
    while (1)
    {
        float humidity = 0.0f;
        float temperature_c = 0.0f;
        esp_err_t res = dht_read_float_data(DHT_TYPE_DHT11, DHT11_GPIO,
                                            &humidity, &temperature_c);
        if (res == ESP_OK) {
            //ESP_LOGI(TAG, "DHT11: %.1f C, %.1f %%", (double)temperature_c, (double)humidity);
            ESP_ERROR_CHECK(oled_display_show_dht11(temperature_c, humidity));
        } else {
            ESP_LOGE(TAG, "DHT11 read failed: %s", esp_err_to_name(res));
            ESP_ERROR_CHECK(oled_display_show_error());
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(buzzer_start_christmas_song_task());
    ESP_ERROR_CHECK(bmp180_init_sensor());
    ESP_ERROR_CHECK(st7735_display_init());
    ESP_ERROR_CHECK(oled_display_init());
    ESP_ERROR_CHECK(ir_nec_transceiver_start());
    xTaskCreate(app_100ms, "app_100ms", configMINIMAL_STACK_SIZE * 15, NULL, 5, NULL);
    xTaskCreate(app_1000ms, "app_1000ms", configMINIMAL_STACK_SIZE * 15, NULL, 5, NULL);
    xTaskCreate(dht11_oled_task, "dht11_oled", configMINIMAL_STACK_SIZE * 15, NULL, 5, NULL);
}
