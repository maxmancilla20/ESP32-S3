#include <inttypes.h>
#include <stdio.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <bmp180.h>
#include <string.h>

#include "bms180_sensor.h"
#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

/*
 * Calibrate this value with the sea-level pressure for the exact location.
 * If Maps shows 1600 m, adjust this value until the estimated altitude
 * matches the actual elevation of the place.
 */
#define REFERENCE_PRESSURE_PA 102560.0f

static bmp180_dev_t g_bmp180;
static bool g_bmp180_initialized = false;

static float bmp180_pressure_to_altitude(uint32_t pressure_pa)
{
    const float altitude = 44330.0f * (1.0f - powf((float)pressure_pa / REFERENCE_PRESSURE_PA, 1.0f / 5.255f));
    return altitude;
}

esp_err_t bmp180_init_sensor(void)
{
    if (g_bmp180_initialized) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(i2cdev_init());

    memset(&g_bmp180, 0, sizeof(g_bmp180));
    ESP_ERROR_CHECK(bmp180_init_desc(&g_bmp180, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(bmp180_init(&g_bmp180));

    g_bmp180_initialized = true;
    return ESP_OK;
}

esp_err_t bmp180_read(bmp180_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t res = bmp180_init_sensor();
    if (res != ESP_OK) {
        return res;
    }

    float temp = 0.0f;
    uint32_t pressure = 0;

    res = bmp180_measure(&g_bmp180, &temp, &pressure, BMP180_MODE_STANDARD);
    if (res != ESP_OK) {
        return res;
    }

    data->temperature_c = temp;
    data->pressure_pa = pressure;
    data->altitude_m = bmp180_pressure_to_altitude(pressure);
    return ESP_OK;
}

void bmp180_test(void *pvParameters)
{
    bmp180_dev_t dev;
    memset(&dev, 0, sizeof(bmp180_dev_t)); // Zero descriptor

    ESP_ERROR_CHECK(bmp180_init_desc(&dev, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(bmp180_init(&dev));

    while (1)
    {
        float temp;
        uint32_t pressure;

        esp_err_t res = bmp180_measure(&dev, &temp, &pressure, BMP180_MODE_STANDARD);
        if (res != ESP_OK)
            printf("Could not measure: %d\n", res);
        else
            /* float is used in printf(). you need non-default configuration in
             * sdkconfig for ESP8266, which is enabled by default for this
             * example. see sdkconfig.defaults.esp8266
             */
            printf("Temperature: %.2f degrees Celsius; Pressure: %" PRIu32 " Pa\n", temp, pressure);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void BMS180_test_task_start(void)
{
    xTaskCreatePinnedToCore(bmp180_test, "bmp180_test", configMINIMAL_STACK_SIZE * 15, NULL, 5, NULL, APP_CPU_NUM);
}