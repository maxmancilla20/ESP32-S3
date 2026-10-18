#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "i2cdev.h"
#include "oled_display.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_I2C_PORT I2C_NUM_0
#define OLED_I2C_ADDRESS 0x3C
#define OLED_I2C_FREQUENCY_HZ 400000
#define OLED_SDA_GPIO GPIO_NUM_18
#define OLED_SCL_GPIO GPIO_NUM_19

static const char *TAG = "oled_display";
static uint8_t framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];
static i2c_dev_t oled_device = {
    .port = OLED_I2C_PORT,
    .addr = OLED_I2C_ADDRESS,
    .cfg = {
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_pullup_en = 1,
        .scl_pullup_en = 1,
        .master.clk_speed = OLED_I2C_FREQUENCY_HZ,
    },
};
static bool oled_initialized;

static const uint8_t *oled_glyph(char character)
{
    static const uint8_t glyphs[][5] = {
        [0] = {0, 0, 0, 0, 0},
        ['A' - ' '] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
        ['C' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x22},
        ['D' - ' '] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
        ['E' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x41},
        ['H' - ' '] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
        ['I' - ' '] = {0x00, 0x41, 0x7F, 0x41, 0x00},
        ['M' - ' '] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
        ['N' - ' '] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
        ['O' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
        ['P' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x06},
        ['R' - ' '] = {0x7F, 0x09, 0x19, 0x29, 0x46},
        ['T' - ' '] = {0x01, 0x01, 0x7F, 0x01, 0x01},
        ['U' - ' '] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
        ['0' - ' '] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
        ['1' - ' '] = {0x00, 0x42, 0x7F, 0x40, 0x00},
        ['2' - ' '] = {0x42, 0x61, 0x51, 0x49, 0x46},
        ['3' - ' '] = {0x21, 0x41, 0x45, 0x4B, 0x31},
        ['4' - ' '] = {0x18, 0x14, 0x12, 0x7F, 0x10},
        ['5' - ' '] = {0x27, 0x45, 0x45, 0x45, 0x39},
        ['6' - ' '] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
        ['7' - ' '] = {0x01, 0x71, 0x09, 0x05, 0x03},
        ['8' - ' '] = {0x36, 0x49, 0x49, 0x49, 0x36},
        ['9' - ' '] = {0x06, 0x49, 0x49, 0x29, 0x1E},
        ['.' - ' '] = {0x00, 0x60, 0x60, 0x00, 0x00},
        ['-' - ' '] = {0x08, 0x08, 0x08, 0x08, 0x08},
        ['%' - ' '] = {0x63, 0x13, 0x08, 0x64, 0x63},
    };

    if (character < ' ' || character > '~') {
        return glyphs[0];
    }
    return glyphs[character - ' '];
}

static void oled_draw_pixel(int x, int y)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    framebuffer[x + (y / 8) * OLED_WIDTH] |= (uint8_t)(1U << (y % 8));
}

static void oled_draw_text(int x, int y, const char *text, int scale)
{
    while (*text != '\0') {
        const uint8_t *glyph = oled_glyph(*text++);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((glyph[column] & (1U << row)) != 0) {
                    for (int dx = 0; dx < scale; ++dx) {
                        for (int dy = 0; dy < scale; ++dy) {
                            oled_draw_pixel(x + column * scale + dx,
                                           y + row * scale + dy);
                        }
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

static esp_err_t oled_write_commands(const uint8_t *commands, size_t length)
{
    uint8_t buffer[32];
    while (length > 0) {
        size_t chunk = length < sizeof(buffer) - 1 ? length : sizeof(buffer) - 1;
        buffer[0] = 0x00;
        memcpy(&buffer[1], commands, chunk);
        ESP_RETURN_ON_ERROR(i2c_dev_write(&oled_device, NULL, 0, buffer, chunk + 1),
                            TAG, "OLED command transfer failed");
        commands += chunk;
        length -= chunk;
    }
    return ESP_OK;
}

static esp_err_t oled_render(void)
{
    for (uint8_t page = 0; page < OLED_HEIGHT / 8; ++page) {
        const uint8_t page_commands[] = {
            (uint8_t)(0xB0 | page), 0x00, 0x10,
        };
        ESP_RETURN_ON_ERROR(oled_write_commands(page_commands, sizeof(page_commands)),
                            TAG, "OLED page selection failed");

        uint8_t data[OLED_WIDTH + 1];
        data[0] = 0x40;
        memcpy(&data[1], &framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        ESP_RETURN_ON_ERROR(i2c_dev_write(&oled_device, NULL, 0, data, sizeof(data)),
                            TAG, "OLED framebuffer transfer failed");
    }
    return ESP_OK;
}

esp_err_t oled_display_init(void)
{
    if (oled_initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2c_dev_create_mutex(&oled_device), TAG, "OLED I2C setup failed");

    const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    ESP_RETURN_ON_ERROR(oled_write_commands(init_commands, sizeof(init_commands)),
                        TAG, "OLED initialization commands failed");
    oled_initialized = true;
    return oled_display_show_error();
}

esp_err_t oled_display_show_dht11(float temperature_c, float humidity)
{
    memset(framebuffer, 0, sizeof(framebuffer));
    oled_draw_text(22, 0, "DHT11", 2);
    oled_draw_text(0, 25, "TMP", 2);

    char value[20];
    snprintf(value, sizeof(value), "%.1fC", (double)temperature_c);
    oled_draw_text(50, 25, value, 2);

    oled_draw_text(0, 44, "HUM", 2);
    snprintf(value, sizeof(value), "%.1f%%", (double)humidity);
    oled_draw_text(50, 44, value, 2);
    return oled_render();
}

esp_err_t oled_display_show_error(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
    oled_draw_text(22, 0, "DHT11", 2);
    oled_draw_text(8, 29, "ERROR", 2);
    return oled_render();
}
