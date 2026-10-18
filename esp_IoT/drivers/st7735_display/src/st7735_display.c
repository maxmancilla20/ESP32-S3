#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7735.h"
#include "esp_log.h"
#include "st7735_display.h"

#define LCD_H_RES 128
#define LCD_V_RES 160
#define LCD_SPI_HOST SPI2_HOST
#define LCD_GPIO_SCLK GPIO_NUM_12
#define LCD_GPIO_MOSI GPIO_NUM_11
#define LCD_GPIO_CS GPIO_NUM_10
#define LCD_GPIO_DC GPIO_NUM_9
#define LCD_GPIO_RST GPIO_NUM_8
#define LCD_GPIO_BL GPIO_NUM_7

static const char *TAG = "st7735_display";
static esp_lcd_panel_handle_t lcd_panel;
static uint16_t lcd_framebuffer[LCD_H_RES * LCD_V_RES];

static uint16_t lcd_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    uint16_t color = ((uint16_t)(red & 0xF8) << 8)
                   | ((uint16_t)(green & 0xFC) << 3)
                   | (blue >> 3);
    return (uint16_t)((color << 8) | (color >> 8));
}

static void lcd_fill_rect(int x, int y, int width, int height, uint16_t color)
{
    for (int row = y; row < y + height; ++row) {
        if (row < 0 || row >= LCD_V_RES) {
            continue;
        }
        for (int column = x; column < x + width; ++column) {
            if (column >= 0 && column < LCD_H_RES) {
                lcd_framebuffer[row * LCD_H_RES + column] = color;
            }
        }
    }
}

static void lcd_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        if (x0 >= 0 && x0 < LCD_H_RES && y0 >= 0 && y0 < LCD_V_RES) {
            lcd_framebuffer[y0 * LCD_H_RES + x0] = color;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void lcd_draw_thermometer(int x, int y, float temperature_c)
{
    const uint16_t outline = lcd_rgb565(174, 205, 220);
    float level = (temperature_c + 10.0f) / 60.0f;
    if (level < 0.0f) {
        level = 0.0f;
    } else if (level > 1.0f) {
        level = 1.0f;
    }
    int fill_height = (int)(level * 14.0f + 0.5f);
    uint16_t mercury = temperature_c < 20.0f
        ? lcd_rgb565(70, 190, 255)
        : lcd_rgb565(255, 90, 75);

    lcd_draw_line(x + 2, y, x + 6, y, outline);
    lcd_draw_line(x + 1, y + 1, x + 1, y + 17, outline);
    lcd_draw_line(x + 7, y + 1, x + 7, y + 17, outline);
    lcd_draw_line(x + 2, y + 18, x + 3, y + 19, outline);
    lcd_draw_line(x + 6, y + 18, x + 5, y + 19, outline);
    lcd_draw_line(x + 3, y + 19, x + 5, y + 19, outline);
    lcd_fill_rect(x + 3, y + 3 + (14 - fill_height), 3, fill_height, mercury);
    lcd_fill_rect(x + 3, y + 15, 3, 3, mercury);
    lcd_fill_rect(x + 2, y + 20, 5, 1, outline);
    lcd_fill_rect(x + 1, y + 21, 7, 1, outline);
    lcd_fill_rect(x, y + 22, 9, 3, mercury);
    lcd_fill_rect(x + 1, y + 25, 7, 1, mercury);
    lcd_fill_rect(x + 2, y + 26, 5, 1, mercury);
}

static void lcd_draw_mountain(int x, int y)
{
    uint16_t slope = lcd_rgb565(87, 205, 150);
    uint16_t snow = lcd_rgb565(225, 243, 250);

    lcd_draw_line(x, y + 24, x + 10, y + 5, slope);
    lcd_draw_line(x + 10, y + 5, x + 17, y + 17, slope);
    lcd_draw_line(x + 17, y + 17, x + 23, y + 8, slope);
    lcd_draw_line(x + 23, y + 8, x + 32, y + 24, slope);
    lcd_draw_line(x, y + 24, x + 32, y + 24, slope);

    lcd_draw_line(x + 7, y + 11, x + 10, y + 5, snow);
    lcd_draw_line(x + 10, y + 5, x + 14, y + 12, snow);
    lcd_draw_line(x + 20, y + 13, x + 23, y + 8, snow);
    lcd_draw_line(x + 23, y + 8, x + 27, y + 15, snow);
}

static uint64_t lcd_glyph(char character)
{
#define GLYPH(a, b, c, d, e) ((uint64_t)(a) | ((uint64_t)(b) << 8) | \
                              ((uint64_t)(c) << 16) | ((uint64_t)(d) << 24) | \
                              ((uint64_t)(e) << 32))
    switch (character) {
    case 'A': return GLYPH(0x7E, 0x11, 0x11, 0x11, 0x7E);
    case 'B': return GLYPH(0x7F, 0x49, 0x49, 0x49, 0x36);
    case 'C': return GLYPH(0x3E, 0x41, 0x41, 0x41, 0x22);
    case 'D': return GLYPH(0x7F, 0x41, 0x41, 0x22, 0x1C);
    case 'E': return GLYPH(0x7F, 0x49, 0x49, 0x49, 0x41);
    case 'F': return GLYPH(0x7F, 0x09, 0x09, 0x09, 0x01);
    case 'G': return GLYPH(0x3E, 0x41, 0x49, 0x49, 0x7A);
    case 'H': return GLYPH(0x7F, 0x08, 0x08, 0x08, 0x7F);
    case 'I': return GLYPH(0x00, 0x41, 0x7F, 0x41, 0x00);
    case 'L': return GLYPH(0x7F, 0x40, 0x40, 0x40, 0x40);
    case 'M': return GLYPH(0x7F, 0x02, 0x0C, 0x02, 0x7F);
    case 'N': return GLYPH(0x7F, 0x04, 0x08, 0x10, 0x7F);
    case 'O': return GLYPH(0x3E, 0x41, 0x41, 0x41, 0x3E);
    case 'P': return GLYPH(0x7F, 0x09, 0x09, 0x09, 0x06);
    case 'R': return GLYPH(0x7F, 0x09, 0x19, 0x29, 0x46);
    case 'S': return GLYPH(0x46, 0x49, 0x49, 0x49, 0x31);
    case 'T': return GLYPH(0x01, 0x01, 0x7F, 0x01, 0x01);
    case 'U': return GLYPH(0x3F, 0x40, 0x40, 0x40, 0x3F);
    case 'V': return GLYPH(0x1F, 0x20, 0x40, 0x20, 0x1F);
    case '0': return GLYPH(0x3E, 0x51, 0x49, 0x45, 0x3E);
    case '1': return GLYPH(0x00, 0x42, 0x7F, 0x40, 0x00);
    case '2': return GLYPH(0x42, 0x61, 0x51, 0x49, 0x46);
    case '3': return GLYPH(0x21, 0x41, 0x45, 0x4B, 0x31);
    case '4': return GLYPH(0x18, 0x14, 0x12, 0x7F, 0x10);
    case '5': return GLYPH(0x27, 0x45, 0x45, 0x45, 0x39);
    case '6': return GLYPH(0x3C, 0x4A, 0x49, 0x49, 0x30);
    case '7': return GLYPH(0x01, 0x71, 0x09, 0x05, 0x03);
    case '8': return GLYPH(0x36, 0x49, 0x49, 0x49, 0x36);
    case '9': return GLYPH(0x06, 0x49, 0x49, 0x29, 0x1E);
    case '.': return GLYPH(0x00, 0x60, 0x60, 0x00, 0x00);
    case '-': return GLYPH(0x08, 0x08, 0x08, 0x08, 0x08);
    default:  return 0;
    }
#undef GLYPH
}

static void lcd_draw_text(int x, int y, const char *text, uint16_t color, int scale)
{
    while (*text != '\0') {
        uint64_t glyph = lcd_glyph(*text++);
        for (int column = 0; column < 5; ++column) {
            uint8_t pixels = (uint8_t)(glyph >> (column * 8));
            for (int row = 0; row < 7; ++row) {
                if ((pixels & (1U << row)) != 0) {
                    lcd_fill_rect(x + column * scale, y + row * scale,
                                  scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static esp_err_t lcd_render(void)
{
    return esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, LCD_H_RES, LCD_V_RES,
                                     lcd_framebuffer);
}

esp_err_t st7735_display_init(void)
{
    const gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << LCD_GPIO_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_config), TAG, "Backlight GPIO setup failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_GPIO_BL, 1), TAG, "Backlight enable failed");

    const spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = sizeof(lcd_framebuffer),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus initialization failed");

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t panel_io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                                 &io_config, &panel_io),
                        TAG, "LCD SPI IO initialization failed");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7735(panel_io, &panel_config, &lcd_panel),
                        TAG, "ST7735 driver initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(lcd_panel), TAG, "LCD reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(lcd_panel), TAG, "LCD initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(lcd_panel, true), TAG, "LCD display enable failed");

    ESP_LOGI(TAG, "ST7735 initialized; backlight enabled");
    return ESP_OK;
}

esp_err_t st7735_display_show_sensor_data(const bmp180_data_t *sensor)
{
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t background = lcd_rgb565(7, 18, 35);
    const uint16_t card = lcd_rgb565(17, 35, 56);
    const uint16_t white = lcd_rgb565(240, 247, 255);
    const uint16_t muted = lcd_rgb565(125, 159, 184);
    char value[24];

    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, background);
    lcd_draw_text(8, 7, "BMP180", lcd_rgb565(42, 220, 220), 2);
    lcd_draw_text(8, 25, "SENSOR I2C", muted, 1);

    lcd_fill_rect(5, 40, 118, 36, card);
    lcd_draw_thermometer(11, 45, sensor->temperature_c);
    lcd_draw_text(34, 44, "TEMP", muted, 1);
    snprintf(value, sizeof(value), "%.1f C", (double)sensor->temperature_c);
    lcd_draw_text(34, 56, value, white, 2);

    lcd_fill_rect(5, 81, 118, 36, card);
    lcd_draw_text(10, 85, "PRESION", muted, 1);
    snprintf(value, sizeof(value), "%.1f HPA", (double)sensor->pressure_pa / 100.0);
    lcd_draw_text(10, 97, value, lcd_rgb565(255, 196, 92), 1);

    lcd_fill_rect(5, 122, 118, 36, card);
    lcd_draw_mountain(10, 126);
    snprintf(value, sizeof(value), "%.0f M", (double)sensor->altitude_m);
    lcd_draw_text(49, 138, value, lcd_rgb565(120, 225, 153), 2);

    return lcd_render();
}

esp_err_t st7735_display_show_sensor_error(void)
{
    const uint16_t background = lcd_rgb565(7, 18, 35);
    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, background);
    lcd_draw_text(8, 7, "BMP180", lcd_rgb565(42, 220, 220), 2);
    lcd_draw_text(8, 54, "SENSOR", lcd_rgb565(255, 105, 105), 2);
    lcd_draw_text(8, 72, "ERROR", lcd_rgb565(255, 105, 105), 2);
    lcd_draw_text(8, 104, "REVISA I2C", lcd_rgb565(240, 247, 255), 1);
    return lcd_render();
}
