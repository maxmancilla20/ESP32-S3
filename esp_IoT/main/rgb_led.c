#include "rgb_led.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"
#include "led_strip.h"

static const char *TAG = "RGB_LED";
static led_strip_t s_led_strip = {
    .type = LED_STRIP_WS2812,
    .is_rgbw = false,
    .brightness = 255,
    .length = 1,
    .gpio = RGB_PIN,
    .channel = RMT_CHANNEL_0,
    .buf = NULL,
};
static bool s_led_initialized;

void init_led_color(void)
{
    if (s_led_initialized) {
        return;
    }

    led_strip_install();
    ESP_ERROR_CHECK(led_strip_init(&s_led_strip));
    s_led_initialized = true;
    ESP_LOGI(TAG, "WS2812 initialized on GPIO %d", RGB_PIN);
    set_led_color(0, 0, 0);
}

void set_led_color(uint32_t r, uint32_t g, uint32_t b)
{
    if (!s_led_initialized) {
        init_led_color();
    }

    const rgb_t color = {
        .r = (uint8_t)r,
        .g = (uint8_t)g,
        .b = (uint8_t)b,
    };
    ESP_ERROR_CHECK(led_strip_set_pixel(&s_led_strip, 0, color));
    ESP_ERROR_CHECK(led_strip_flush(&s_led_strip));
}
