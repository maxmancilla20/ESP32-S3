#include "ir_nec_transceiver.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ir_nec_encoder.h"
#include "rgb_led.h"

#define IR_TX_RESOLUTION_HZ 1000000
#define IR_CAPTURE_MAX_PULSES 256
#define IR_CAPTURE_END_GAP_US 60000
#define IR_NEC_MARGIN_US 350
#define IR_EDGE_QUEUE_LENGTH 256

static const char *TAG = "IR_NEC";
typedef struct {
    int64_t timestamp_us;
    uint8_t rx_level;
} ir_edge_event_t;

static QueueHandle_t s_edge_queue;
static SemaphoreHandle_t s_frame_mutex;
static rmt_channel_handle_t s_tx_channel;
static rmt_encoder_handle_t s_nec_encoder;
static rmt_encoder_handle_t s_captured_tail_encoder;
static ir_pulse_t s_capture_pulses[IR_CAPTURE_MAX_PULSES];
static ir_pulse_t s_last_frame_pulses[IR_CAPTURE_MAX_PULSES];
static rmt_symbol_word_t s_tx_symbols[IR_CAPTURE_MAX_PULSES];
static size_t s_capture_pulse_count;
static size_t s_last_frame_pulse_count;
static bool s_capture_active;
static bool s_last_frame_available;
static volatile bool s_edge_queue_overflow;
static int64_t s_previous_edge_us;
static uint8_t s_previous_rx_level;

static bool duration_matches(uint32_t duration, uint32_t expected)
{
    return duration > expected
        ? duration - expected <= IR_NEC_MARGIN_US
        : expected - duration <= IR_NEC_MARGIN_US;
}

static bool parse_nec_bit(const ir_pulse_t *mark, const ir_pulse_t *space,
                          uint16_t *value, unsigned bit)
{
    if (mark->level != 1 || space->level != 0 ||
        !duration_matches(mark->duration_us, 560)) return false;
    if (duration_matches(space->duration_us, 560)) {
        *value &= (uint16_t)~(1U << bit);
        return true;
    }
    if (duration_matches(space->duration_us, 1690)) {
        *value |= (uint16_t)(1U << bit);
        return true;
    }
    return false;
}

static void apply_nec_command(uint8_t address, uint8_t command)
{
    if (address != 0x00) return;

    switch (command) {
    case 0x43:
        set_led_color(255, 255, 255);
        printf("Accion: luz blanca\r\n");
        break;
    case 0x44:
        set_led_color(255, 160, 80);
        printf("Accion: luz calida\r\n");
        break;
    case 0x40:
        set_led_color(255, 215, 160);
        printf("Accion: luz media\r\n");
        break;
    default:
        break;
    }
}

static void print_raw_frame(const ir_pulse_t *pulses, size_t pulse_count)
{
    printf("IR capture: %u pulses, durations in us\r\n", (unsigned)pulse_count);
    for (size_t i = 0; i < pulse_count; i++) {
        printf("{%u:%" PRIu32 "}%s", (unsigned)pulses[i].level,
               pulses[i].duration_us, (i + 1) % 4 == 0 ? "\r\n" : " ");
    }
    if (pulse_count % 4 != 0) printf("\r\n");
}

static void print_ir_frame(const ir_pulse_t *pulses, size_t pulse_count)
{
    if (pulse_count == 4 && pulses[0].level == 1 && pulses[1].level == 0 &&
        duration_matches(pulses[0].duration_us, 9000) &&
        duration_matches(pulses[1].duration_us, 2250)) {
        printf("NEC repeat\r\n");
        return;
    }

    if (pulse_count < 67 || pulses[0].level != 1 || pulses[1].level != 0 ||
        !duration_matches(pulses[0].duration_us, 9000) ||
        !duration_matches(pulses[1].duration_us, 4500)) {
        print_raw_frame(pulses, pulse_count);
        return;
    }

    uint16_t address = 0;
    uint16_t command = 0;
    for (unsigned bit = 0; bit < 16; bit++) {
        const size_t address_mark = 2 + bit * 2;
        const size_t command_mark = 34 + bit * 2;
        if (!parse_nec_bit(&pulses[address_mark], &pulses[address_mark + 1],
                           &address, bit) ||
            !parse_nec_bit(&pulses[command_mark], &pulses[command_mark + 1],
                           &command, bit)) {
            printf("Invalid NEC timing; raw capture follows\r\n");
            print_raw_frame(pulses, pulse_count);
            return;
        }
    }
    if (pulses[66].level != 1 || !duration_matches(pulses[66].duration_us, 560)) {
        printf("Invalid NEC ending pulse; raw capture follows\r\n");
        print_raw_frame(pulses, pulse_count);
        return;
    }
    const uint8_t address_byte = (uint8_t)address;
    const uint8_t address_inverse = (uint8_t)(address >> 8);
    const uint8_t command_byte = (uint8_t)command;
    const uint8_t command_inverse = (uint8_t)(command >> 8);
    const bool complements_valid =
        (uint8_t)(address_byte ^ address_inverse) == 0xFF &&
        (uint8_t)(command_byte ^ command_inverse) == 0xFF;

    printf("NEC address=0x%02X address_inv=0x%02X command=0x%02X command_inv=0x%02X%s\r\n",
           address_byte, address_inverse, command_byte, command_inverse,
           complements_valid ? "" : " (complement check failed)");
    if (complements_valid) {
        apply_nec_command(address_byte, command_byte);
    }
}

static void IRAM_ATTR gpio_edge_callback(void *arg)
{
    ir_edge_event_t event = {
        .timestamp_us = esp_timer_get_time(),
        .rx_level = (uint8_t)gpio_get_level(IR_RX_GPIO_NUM),
    };
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_edge_queue, &event, &higher_priority_task_woken) != pdPASS) {
        s_edge_queue_overflow = true;
    }
    if (higher_priority_task_woken == pdTRUE) portYIELD_FROM_ISR();
}

esp_err_t ir_nec_transceiver_send(uint16_t address_word, uint16_t command_word)
{
    if (s_tx_channel == NULL || s_nec_encoder == NULL) return ESP_ERR_INVALID_STATE;

    const ir_nec_scan_code_t scan_code = {
        .address = address_word,
        .command = command_word,
    };
    const rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(s_tx_channel, s_nec_encoder, &scan_code,
                                 sizeof(scan_code), &transmit_config);
    if (err != ESP_OK) return err;
    return rmt_tx_wait_all_done(s_tx_channel, -1);
}

const ir_pulse_t *ir_nec_transceiver_get_last_frame(size_t *pulse_count)
{
    if (s_frame_mutex == NULL || xSemaphoreTake(s_frame_mutex, portMAX_DELAY) != pdTRUE) {
        if (pulse_count != NULL) *pulse_count = 0;
        return NULL;
    }
    if (pulse_count != NULL) *pulse_count = s_last_frame_pulse_count;
    const ir_pulse_t *pulses = s_last_frame_available ? s_last_frame_pulses : NULL;
    xSemaphoreGive(s_frame_mutex);
    return pulses;
}

static bool append_tx_interval(size_t *segment_count, uint8_t level, uint32_t duration_us)
{
    uint64_t ticks = ((uint64_t)duration_us * IR_TX_RESOLUTION_HZ + 500000) / 1000000;
    if (ticks == 0) ticks = 1;

    while (ticks > 0) {
        if (*segment_count >= IR_CAPTURE_MAX_PULSES * 2) return false;
        const uint16_t duration = ticks > 0x7FFF ? 0x7FFF : (uint16_t)ticks;
        rmt_symbol_word_t *symbol = &s_tx_symbols[*segment_count / 2];
        if ((*segment_count & 1U) == 0) {
            symbol->level0 = level;
            symbol->duration0 = duration;
        } else {
            symbol->level1 = level;
            symbol->duration1 = duration;
        }
        (*segment_count)++;
        ticks -= duration;
    }
    return true;
}

esp_err_t ir_nec_transceiver_replay_last_frame(void)
{
    if (s_tx_channel == NULL || s_captured_tail_encoder == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_frame_mutex == NULL || xSemaphoreTake(s_frame_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const size_t pulse_count = s_last_frame_pulse_count;
    if (!s_last_frame_available || pulse_count == 0) {
        xSemaphoreGive(s_frame_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    size_t segment_count = 0;
    for (size_t i = 0; i < pulse_count; i++) {
        if (!append_tx_interval(&segment_count, s_last_frame_pulses[i].level,
                                s_last_frame_pulses[i].duration_us)) {
            xSemaphoreGive(s_frame_mutex);
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if ((segment_count & 1U) != 0) {
        rmt_symbol_word_t *symbol = &s_tx_symbols[segment_count / 2];
        symbol->level1 = 0;
        symbol->duration1 = 1;
        segment_count++;
    }

    esp_err_t err = gpio_intr_disable(IR_RX_GPIO_NUM);
    if (err != ESP_OK) {
        xSemaphoreGive(s_frame_mutex);
        return err;
    }
    s_capture_active = false;
    s_capture_pulse_count = 0;
    xQueueReset(s_edge_queue);

    const rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    err = rmt_transmit(s_tx_channel, s_captured_tail_encoder,
                       s_tx_symbols,
                       (segment_count / 2) * sizeof(rmt_symbol_word_t),
                       &transmit_config);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(s_tx_channel, -1);
    xQueueReset(s_edge_queue);
    const esp_err_t enable_err = gpio_intr_enable(IR_RX_GPIO_NUM);
    xSemaphoreGive(s_frame_mutex);
    return err != ESP_OK ? err : enable_err;
}

static void finish_capture(int64_t end_timestamp_us)
{
    if (!s_capture_active) return;
    s_capture_active = false;
    if (s_capture_pulse_count == 0) return;

    const int64_t final_gap_us = end_timestamp_us - s_previous_edge_us;
    if (s_capture_pulse_count < IR_CAPTURE_MAX_PULSES) {
        s_capture_pulses[s_capture_pulse_count++] = (ir_pulse_t) {
            .level = (uint8_t)!s_previous_rx_level,
            .duration_us = final_gap_us > UINT32_MAX ? UINT32_MAX : (uint32_t)final_gap_us,
        };
    }
    if (xSemaphoreTake(s_frame_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(s_last_frame_pulses, s_capture_pulses,
               s_capture_pulse_count * sizeof(s_capture_pulses[0]));
        s_last_frame_pulse_count = s_capture_pulse_count;
        s_last_frame_available = true;
        xSemaphoreGive(s_frame_mutex);
    }
    ESP_LOGI(TAG, "Captured %zu GPIO pulse intervals", s_last_frame_pulse_count);
    print_ir_frame(s_last_frame_pulses, s_last_frame_pulse_count);
    vTaskDelay(pdMS_TO_TICKS(3000));
    const esp_err_t replay_result = ir_nec_transceiver_replay_last_frame();
    if (replay_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to replay captured frame: %s",
                 esp_err_to_name(replay_result));
    } else {
        ESP_LOGI(TAG, "Captured frame replayed on GPIO %d", IR_TX_GPIO_NUM);
    }
    s_capture_pulse_count = 0;
}

static void begin_capture(const ir_edge_event_t *event)
{
    s_capture_active = true;
    s_capture_pulse_count = 0;
    s_previous_edge_us = event->timestamp_us;
    s_previous_rx_level = event->rx_level;
}

static void process_edge(const ir_edge_event_t *event)
{
    if (!s_capture_active) {
        begin_capture(event);
        return;
    }

    const int64_t elapsed_us = event->timestamp_us - s_previous_edge_us;
    if (elapsed_us >= IR_CAPTURE_END_GAP_US) {
        finish_capture(event->timestamp_us);
        begin_capture(event);
        return;
    }

    if (elapsed_us > 0 && s_capture_pulse_count < IR_CAPTURE_MAX_PULSES) {
        s_capture_pulses[s_capture_pulse_count++] = (ir_pulse_t) {
            .level = (uint8_t)!s_previous_rx_level,
            .duration_us = (uint32_t)elapsed_us,
        };
    }
    s_previous_edge_us = event->timestamp_us;
    s_previous_rx_level = event->rx_level;
}

static void ir_receive_task(void *arg)
{
    ir_edge_event_t event;
    while (true) {
        if (xQueueReceive(s_edge_queue, &event, pdMS_TO_TICKS(10)) == pdPASS) {
            if (s_edge_queue_overflow) {
                s_edge_queue_overflow = false;
                s_capture_active = false;
                s_capture_pulse_count = 0;
                xQueueReset(s_edge_queue);
                ESP_LOGW(TAG, "GPIO edge queue overflow; discarded partial capture");
                continue;
            }
            process_edge(&event);
        } else if (s_capture_active &&
                   esp_timer_get_time() - s_previous_edge_us >= IR_CAPTURE_END_GAP_US) {
            finish_capture(esp_timer_get_time());
        }
    }
}

esp_err_t ir_nec_transceiver_start(void)
{
    const gpio_config_t rx_gpio_config = {
        .pin_bit_mask = 1ULL << IR_RX_GPIO_NUM,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&rx_gpio_config);
    if (err != ESP_OK) return err;

    s_edge_queue = xQueueCreate(IR_EDGE_QUEUE_LENGTH, sizeof(ir_edge_event_t));
    if (s_edge_queue == NULL) return ESP_ERR_NO_MEM;
    s_frame_mutex = xSemaphoreCreateMutex();
    if (s_frame_mutex == NULL) return ESP_ERR_NO_MEM;

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = gpio_isr_handler_add(IR_RX_GPIO_NUM, gpio_edge_callback, NULL);
    if (err != ESP_OK) return err;

    const rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_TX_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = IR_TX_GPIO_NUM,
    };
    err = rmt_new_tx_channel(&tx_config, &s_tx_channel);
    if (err != ESP_OK) return err;

    const rmt_carrier_config_t carrier_config = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33,
    };
    err = rmt_apply_carrier(s_tx_channel, &carrier_config);
    if (err != ESP_OK) return err;

    const ir_nec_encoder_config_t encoder_config = { .resolution = IR_TX_RESOLUTION_HZ };
    err = rmt_new_ir_nec_encoder(&encoder_config, &s_nec_encoder);
    if (err != ESP_OK) return err;

    rmt_copy_encoder_config_t tail_config = {};
    err = rmt_new_copy_encoder(&tail_config, &s_captured_tail_encoder);
    if (err != ESP_OK) return err;

    err = rmt_enable(s_tx_channel);
    if (err != ESP_OK) return err;

    if (xTaskCreate(ir_receive_task, "ir_nec_rx", configMINIMAL_STACK_SIZE * 4,
                    NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    err = gpio_intr_enable(IR_RX_GPIO_NUM);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "NEC transceiver ready: RX GPIO %d, TX GPIO %d",
             IR_RX_GPIO_NUM, IR_TX_GPIO_NUM);
    return ESP_OK;
}