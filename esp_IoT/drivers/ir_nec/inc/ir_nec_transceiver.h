#ifndef IR_NEC_TRANSCEIVER_H
#define IR_NEC_TRANSCEIVER_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define IR_RX_GPIO_NUM 4
#define IR_TX_GPIO_NUM 5

typedef struct {
	uint8_t level;
	uint32_t duration_us;
} ir_pulse_t;

esp_err_t ir_nec_transceiver_start(void);
esp_err_t ir_nec_transceiver_send(uint16_t address_word, uint16_t command_word);
esp_err_t ir_nec_transceiver_replay_last_frame(void);

const ir_pulse_t *ir_nec_transceiver_get_last_frame(size_t *pulse_count);

#endif