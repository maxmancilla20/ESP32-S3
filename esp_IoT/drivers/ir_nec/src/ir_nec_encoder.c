/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include "esp_check.h"
#include "ir_nec_encoder.h"

static const char *TAG = "nec_encoder";

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    rmt_encoder_t *bytes_encoder;
    rmt_symbol_word_t leading_symbol;
    rmt_symbol_word_t ending_symbol;
    int state;
} ir_nec_encoder_t;

RMT_ENCODER_FUNC_ATTR
static size_t encode_ir_nec(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *data, size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    ir_nec_encoder_t *nec_encoder = __containerof(encoder, ir_nec_encoder_t, base);
    const ir_nec_scan_code_t *scan_code = data;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (nec_encoder->state) {
    case 0:
        encoded_symbols += nec_encoder->copy_encoder->encode(
            nec_encoder->copy_encoder, channel, &nec_encoder->leading_symbol,
            sizeof(nec_encoder->leading_symbol), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) nec_encoder->state = 1;
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto done;
        }
        /* fall through */
    case 1:
        encoded_symbols += nec_encoder->bytes_encoder->encode(
            nec_encoder->bytes_encoder, channel, &scan_code->address,
            sizeof(scan_code->address), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) nec_encoder->state = 2;
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto done;
        }
        /* fall through */
    case 2:
        encoded_symbols += nec_encoder->bytes_encoder->encode(
            nec_encoder->bytes_encoder, channel, &scan_code->command,
            sizeof(scan_code->command), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) nec_encoder->state = 3;
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto done;
        }
        /* fall through */
    case 3:
        encoded_symbols += nec_encoder->copy_encoder->encode(
            nec_encoder->copy_encoder, channel, &nec_encoder->ending_symbol,
            sizeof(nec_encoder->ending_symbol), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = 0;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) state |= RMT_ENCODING_MEM_FULL;
        break;
    default:
        break;
    }

done:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t delete_ir_nec_encoder(rmt_encoder_t *encoder)
{
    ir_nec_encoder_t *nec_encoder = __containerof(encoder, ir_nec_encoder_t, base);
    rmt_del_encoder(nec_encoder->copy_encoder);
    rmt_del_encoder(nec_encoder->bytes_encoder);
    free(nec_encoder);
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t reset_ir_nec_encoder(rmt_encoder_t *encoder)
{
    ir_nec_encoder_t *nec_encoder = __containerof(encoder, ir_nec_encoder_t, base);
    ESP_RETURN_ON_ERROR(rmt_encoder_reset(nec_encoder->copy_encoder), TAG, "reset copy encoder");
    ESP_RETURN_ON_ERROR(rmt_encoder_reset(nec_encoder->bytes_encoder), TAG, "reset bytes encoder");
    nec_encoder->state = 0;
    return ESP_OK;
}

esp_err_t rmt_new_ir_nec_encoder(const ir_nec_encoder_config_t *config,
                                 rmt_encoder_handle_t *ret_encoder)
{
    ESP_RETURN_ON_FALSE(config != NULL && ret_encoder != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "null encoder config or output");

    ir_nec_encoder_t *nec_encoder = rmt_alloc_encoder_mem(sizeof(*nec_encoder));
    ESP_RETURN_ON_FALSE(nec_encoder != NULL, ESP_ERR_NO_MEM, TAG, "encoder allocation failed");
    nec_encoder->base.encode = encode_ir_nec;
    nec_encoder->base.del = delete_ir_nec_encoder;
    nec_encoder->base.reset = reset_ir_nec_encoder;
    nec_encoder->state = 0;

    rmt_copy_encoder_config_t copy_config = {};
    esp_err_t err = rmt_new_copy_encoder(&copy_config, &nec_encoder->copy_encoder);
    if (err != ESP_OK) goto fail;

#define US_TO_TICKS(duration_us) \
    ((uint32_t)(((uint64_t)(duration_us) * config->resolution + 500000) / 1000000))
    nec_encoder->leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1, .duration0 = US_TO_TICKS(9000),
        .level1 = 0, .duration1 = US_TO_TICKS(4500),
    };
    nec_encoder->ending_symbol = (rmt_symbol_word_t) {
        .level0 = 1, .duration0 = US_TO_TICKS(560),
        .level1 = 0, .duration1 = 0x7FFF,
    };

    rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = { .level0 = 1, .duration0 = US_TO_TICKS(560),
                  .level1 = 0, .duration1 = US_TO_TICKS(560) },
        .bit1 = { .level0 = 1, .duration0 = US_TO_TICKS(560),
                  .level1 = 0, .duration1 = US_TO_TICKS(1690) },
    };
#undef US_TO_TICKS
    err = rmt_new_bytes_encoder(&bytes_config, &nec_encoder->bytes_encoder);
    if (err != ESP_OK) goto fail;

    *ret_encoder = &nec_encoder->base;
    return ESP_OK;

fail:
    if (nec_encoder->copy_encoder != NULL) rmt_del_encoder(nec_encoder->copy_encoder);
    if (nec_encoder->bytes_encoder != NULL) rmt_del_encoder(nec_encoder->bytes_encoder);
    free(nec_encoder);
    return err;
}