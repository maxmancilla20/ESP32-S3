#include "rgb_led.h"

#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "RGB_LED";
static led_strip_handle_t s_led_strip;
static bool s_led_initialized;

void init_led_color(void)
{
    if (s_led_initialized) {
        return;
    }

    const led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_PIN,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags = {
            .with_dma = false,
        },
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
    s_led_initialized = true;
    ESP_LOGI(TAG, "WS2812 initialized on GPIO %d", RGB_PIN);
    ESP_ERROR_CHECK(led_strip_clear(s_led_strip));
}

void set_led_color(uint32_t r, uint32_t g, uint32_t b)
{
    if (!s_led_initialized) {
        init_led_color();
    }

    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0,
                                        (uint8_t)r, (uint8_t)g, (uint8_t)b));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}
