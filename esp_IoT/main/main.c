#include <stddef.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "st7735_display.h"
#include "dht.h"

#define LIGHT_SAMPLE_COUNT 8
#define LIGHT_TASK_PERIOD_MS 1000
#define SOIL_SAMPLE_COUNT 8
#define LIGHT_CAL_ADC_AT_9_LUX 2878.0f
#define LIGHT_CAL_ADC_AT_24_LUX 2335.5f
#define LIGHT_CAL_ADC_AT_62_LUX 1558.6f
#define LIGHT_CAL_LOW_LUX 9.0f
#define LIGHT_CAL_MID_LUX 24.0f
#define LIGHT_CAL_HIGH_LUX 62.0f

static const char *TAG = "esp32c3_supermini";
static adc_oneshot_unit_handle_t sensor_adc_handle;
static adc_channel_t light_adc_channel;
static adc_channel_t soil_adc_channel;
static SemaphoreHandle_t readings_mutex;

typedef struct {
    bool dht_valid;
    float temperature_c;
    float humidity;
    bool soil_valid;
    int soil_adc_raw;
    bool light_valid;
    int light_lux;
} sensor_readings_t;

static sensor_readings_t readings;

static int light_adc_to_lux(int adc_raw)
{
    float lux;
    if (adc_raw >= LIGHT_CAL_ADC_AT_24_LUX) {
        lux = LIGHT_CAL_LOW_LUX
            + ((float)adc_raw - LIGHT_CAL_ADC_AT_9_LUX)
            * (LIGHT_CAL_MID_LUX - LIGHT_CAL_LOW_LUX)
            / (LIGHT_CAL_ADC_AT_24_LUX - LIGHT_CAL_ADC_AT_9_LUX);
    } else {
        lux = LIGHT_CAL_MID_LUX
            + ((float)adc_raw - LIGHT_CAL_ADC_AT_24_LUX)
            * (LIGHT_CAL_HIGH_LUX - LIGHT_CAL_MID_LUX)
            / (LIGHT_CAL_ADC_AT_62_LUX - LIGHT_CAL_ADC_AT_24_LUX);
    }

    if (lux < 0.0f) {
        lux = 0.0f;
    }
    return (int)(lux + 0.5f);
}

static void dht11_task(void *arg)
{
    while (1) {
        float humidity = 0.0f;
        float temperature_c = 0.0f;
        esp_err_t result = dht_read_float_data(DHT_TYPE_DHT11, BOARD_PIN_DHT11,
                                               &humidity, &temperature_c);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "DHT11: %.1f C, %.1f %%", (double)temperature_c,
                     (double)humidity);
            ESP_ERROR_CHECK(xSemaphoreTake(readings_mutex, portMAX_DELAY) == pdTRUE
                                ? ESP_OK : ESP_FAIL);
            readings.dht_valid = true;
            readings.temperature_c = temperature_c;
            readings.humidity = humidity;
            esp_err_t display_result = st7735_display_show_readings(
                readings.dht_valid, readings.temperature_c, readings.humidity,
                readings.soil_valid, readings.soil_adc_raw,
                readings.light_valid, readings.light_lux);
            xSemaphoreGive(readings_mutex);
            ESP_ERROR_CHECK(display_result);
        } else {
            ESP_LOGE(TAG, "DHT11 read failed: %s", esp_err_to_name(result));
            ESP_ERROR_CHECK(xSemaphoreTake(readings_mutex, portMAX_DELAY) == pdTRUE
                                ? ESP_OK : ESP_FAIL);
            readings.dht_valid = false;
            esp_err_t display_result = st7735_display_show_readings(
                readings.dht_valid, readings.temperature_c, readings.humidity,
                readings.soil_valid, readings.soil_adc_raw,
                readings.light_valid, readings.light_lux);
            xSemaphoreGive(readings_mutex);
            ESP_ERROR_CHECK(display_result);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void sensor_task(void *arg)
{
    while (1) {
        int raw_sum = 0;
        esp_err_t light_result = ESP_OK;
        for (int i = 0; i < LIGHT_SAMPLE_COUNT; ++i) {
            int sample = 0;
            light_result = adc_oneshot_read(sensor_adc_handle, light_adc_channel,
                                            &sample);
            if (light_result != ESP_OK) {
                break;
            }
            raw_sum += sample;
        }

        int soil_raw_sum = 0;
        esp_err_t soil_result = ESP_OK;
        for (int i = 0; i < SOIL_SAMPLE_COUNT; ++i) {
            int sample = 0;
            soil_result = adc_oneshot_read(sensor_adc_handle, soil_adc_channel,
                                           &sample);
            if (soil_result != ESP_OK) {
                break;
            }
            soil_raw_sum += sample;
        }

        ESP_ERROR_CHECK(xSemaphoreTake(readings_mutex, portMAX_DELAY) == pdTRUE
                            ? ESP_OK : ESP_FAIL);
        if (light_result == ESP_OK) {
            readings.light_valid = true;
            int light_raw = (raw_sum + LIGHT_SAMPLE_COUNT / 2)
                            / LIGHT_SAMPLE_COUNT;
            readings.light_lux = light_adc_to_lux(light_raw);
            //ESP_LOGI(TAG, "Light sensor: %d lux (ADC raw: %d)",
            //         readings.light_lux, light_raw);
        } else {
            readings.light_valid = false;
            ESP_LOGE(TAG, "Light sensor ADC read failed: %s",
                     esp_err_to_name(light_result));
        }
        if (soil_result == ESP_OK) {
            readings.soil_valid = true;
            readings.soil_adc_raw = (soil_raw_sum + SOIL_SAMPLE_COUNT / 2)
                                    / SOIL_SAMPLE_COUNT;
            //ESP_LOGI(TAG, "Soil sensor ADC raw: %d", readings.soil_adc_raw);
        } else {
            readings.soil_valid = false;
            ESP_LOGE(TAG, "Soil sensor ADC read failed: %s",
                     esp_err_to_name(soil_result));
        }
        esp_err_t display_result = st7735_display_show_readings(
            readings.dht_valid, readings.temperature_c, readings.humidity,
            readings.soil_valid, readings.soil_adc_raw,
            readings.light_valid, readings.light_lux);
        xSemaphoreGive(readings_mutex);
        ESP_ERROR_CHECK(display_result);

        vTaskDelay(pdMS_TO_TICKS(LIGHT_TASK_PERIOD_MS));
    }
}

static void sensor_adc_init(void)
{
    adc_unit_t light_unit;
    adc_unit_t soil_unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(BOARD_PIN_LIGHT_ADC, &light_unit,
                                              &light_adc_channel));
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(BOARD_PIN_SOIL_ADC, &soil_unit,
                                              &soil_adc_channel));
    ESP_ERROR_CHECK(light_unit == ADC_UNIT_1 && soil_unit == light_unit
                        ? ESP_OK : ESP_ERR_INVALID_ARG);

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = light_unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &sensor_adc_handle));

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(sensor_adc_handle,
                                               light_adc_channel,
                                               &channel_config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(sensor_adc_handle,
                                               soil_adc_channel,
                                               &channel_config));
    ESP_LOGI(TAG, "Light ADC ready on GPIO %d (ADC1 channel %d)",
             BOARD_PIN_LIGHT_ADC, light_adc_channel);
    ESP_LOGI(TAG, "Soil ADC ready on GPIO %d (ADC1 channel %d)",
             BOARD_PIN_SOIL_ADC, soil_adc_channel);
}

void app_main(void)
{
    ESP_ERROR_CHECK(st7735_display_init());
    readings_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(readings_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    sensor_adc_init();

    BaseType_t task_created = xTaskCreate(dht11_task, "dht11",
                                          configMINIMAL_STACK_SIZE * 20,
                                          NULL, 5, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_FAIL);

    task_created = xTaskCreate(sensor_task, "sensors",
                               configMINIMAL_STACK_SIZE * 4,
                               NULL, 5, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_FAIL);
}
