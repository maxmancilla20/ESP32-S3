#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "buzzer.h"

#define BUZZER_GPIO GPIO_NUM_13
#define BUZZER_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4)
#define BUZZER_DUTY (1U << (LEDC_TIMER_10_BIT - 1))
#define BUZZER_NOTE_GAP_MS 80
#define BUZZER_TEMPO_NUMERATOR 3
#define BUZZER_TEMPO_DENOMINATOR 2

static const char *TAG = "buzzer";

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
} buzzer_note_t;

enum {
    NOTE_C4 = 262,
    NOTE_D4 = 294,
    NOTE_E4 = 330,
    NOTE_F4 = 349,
    NOTE_G4 = 392,
    REST = 0,
};

static const buzzer_note_t christmas_melody[] = {
    {NOTE_E4, 250}, {NOTE_E4, 250}, {NOTE_E4, 400},
    {NOTE_E4, 250}, {NOTE_E4, 250}, {NOTE_E4, 400},
    {NOTE_E4, 250}, {NOTE_G4, 250}, {NOTE_C4, 250},
    {NOTE_D4, 250}, {NOTE_E4, 550},
    {NOTE_F4, 250}, {NOTE_F4, 250}, {NOTE_F4, 250},
    {NOTE_F4, 250}, {NOTE_F4, 250}, {NOTE_E4, 250},
    {NOTE_E4, 250}, {NOTE_E4, 180}, {NOTE_E4, 180},
    {NOTE_D4, 250}, {NOTE_D4, 250}, {NOTE_E4, 250},
    {NOTE_D4, 400}, {NOTE_G4, 550},
    {REST, 450},
};

static esp_err_t buzzer_play_note(const buzzer_note_t *note)
{
    TickType_t note_duration = pdMS_TO_TICKS(
        (note->duration_ms * BUZZER_TEMPO_NUMERATOR) / BUZZER_TEMPO_DENOMINATOR);

    if (note->frequency_hz == REST) {
        vTaskDelay(note_duration);
        return ESP_OK;
    }

    esp_err_t result = ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, note->frequency_hz);
    if (result != ESP_OK) {
        return result;
    }
    result = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, BUZZER_DUTY);
    if (result != ESP_OK) {
        return result;
    }
    result = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (result != ESP_OK) {
        return result;
    }

    vTaskDelay(note_duration);
    result = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    if (result != ESP_OK) {
        return result;
    }
    result = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(BUZZER_NOTE_GAP_MS));
    return ESP_OK;
}

static void buzzer_christmas_song_task(void *arg)
{
    while (true) {
        for (size_t i = 0; i < sizeof(christmas_melody) / sizeof(christmas_melody[0]); ++i) {
            esp_err_t result = buzzer_play_note(&christmas_melody[i]);
            if (result != ESP_OK) {
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                ESP_LOGE(TAG, "Could not play melody note: %s", esp_err_to_name(result));
                vTaskDelete(NULL);
                return;
            }
        }
    }
}

esp_err_t buzzer_start_christmas_song_task(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = NOTE_E4,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "LEDC timer setup failed");

    const ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "LEDC channel setup failed");

    BaseType_t result = xTaskCreate(buzzer_christmas_song_task, "christmas_song",
                                    BUZZER_TASK_STACK_SIZE, NULL, 4, NULL);
    if (result != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
