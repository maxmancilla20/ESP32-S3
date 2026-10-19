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
#include "esp_timer.h"
#include "board_pins.h"
#include "st7735_display.h"

#define LCD_H_RES 128
#define LCD_V_RES 160
#define LCD_SPI_HOST SPI2_HOST
#define LCD_GPIO_SCLK BOARD_PIN_TFT_SCLK
#define LCD_GPIO_MOSI BOARD_PIN_TFT_MOSI
#define LCD_GPIO_CS BOARD_PIN_TFT_CS
#define LCD_GPIO_DC BOARD_PIN_TFT_DC
#define LCD_GPIO_RST BOARD_PIN_TFT_RESET

static const char *TAG = "st7735_display";
static esp_lcd_panel_handle_t lcd_panel;
static uint16_t lcd_framebuffer[LCD_H_RES * LCD_V_RES];

static uint16_t lcd_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    uint16_t color = ((uint16_t)(red & 0xF8) << 8)
                   | ((uint16_t)(green & 0xFC) << 3)
                   | (blue >> 3);
    /* The panel expects the RGB565 bytes in swapped order. */
    return (uint16_t)((color << 8) | (color >> 8));
}

/* Clip rectangles to the framebuffer before writing their pixels. */
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
    /* Bresenham's algorithm draws a line using integer pixel coordinates. */
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

static void lcd_fill_circle(int center_x, int center_y, int radius,
                            uint16_t color)
{
    for (int y = -radius; y <= radius; ++y) {
        int extent = 0;
        while ((extent + 1) * (extent + 1) + y * y <= radius * radius) {
            ++extent;
        }
        lcd_fill_rect(center_x - extent, center_y + y, 2 * extent + 1, 1,
                      color);
    }
}

static void lcd_draw_mqtt_status(int center_x, int center_y, bool connected)
{
    const uint16_t status = connected
        ? lcd_rgb565(55, 210, 65)
        : lcd_rgb565(0, 0, 240);

    lcd_fill_circle(center_x, center_y, 2, status);
}

static const char fan_icon[16][17] = {
    ".23.72AA..27324.", ".463A.....AA446.", ".42.EE......A66.",
    ".2A.ED9......A7.", ".7..ED9..EEEE.7.", "....EE9.EE99D.A.",
    ".A...E9999......", ".......EE.......", ".A....999DE.....",
    "...D99D..9EE..A.", "...EEEE..9EE....", ".4A.EE...9EE.A2.",
    ".2.A.....DE..26.", ".466A......A664.", ".6442...A.32466.",
    ".33.323.4426364.",
};

static const char sprinkler_icon[16][17] = {
    "..............3.", "............388.", "..........39883.",
    "....33BCC3939...", "...92BCCCCC93...", "...383BCCCCC3...",
    "....893BCCCC3...", ".....293BCCC3...", "..33..293BCCD...",
    "..33...293BC3...", "....33..293D....", ".33.F3...293....",
    "3F....33..3.....", ".3.33.33........", "..3F3...........",
    "...3............",
};

static const char bulb_icon[16][17] = {
    "......1111......", ".....112211.....",
    "....12222221....", "...1222222221...",
    "..122222222221..", "..122222222221..",
    "..122222222221..", "...1222222221...",
    "....12222221....", ".....122221.....",
    "......1221......", "......1331......",
    ".....133331.....", ".....133331.....",
    "......1111......", ".......11.......",
};

static uint8_t lcd_hex_digit(char character)
{
    if (character >= '0' && character <= '9') {
        return (uint8_t)(character - '0');
    }
    return (uint8_t)(character - 'A' + 10);
}

static void lcd_draw_indexed_icon(int x, int y, const char rows[16][17],
                                  const uint16_t palette[16])
{
    for (int row = 0; row < 16; ++row) {
        for (int column = 0; column < 16; ++column) {
            char pixel = rows[row][column];
            if (pixel != '.') {
                lcd_fill_rect(x + column, y + row, 1, 1,
                              palette[lcd_hex_digit(pixel)]);
            }
        }
    }
}

static void lcd_draw_thermometer(int x, int y, float temperature_c)
{
    const uint16_t outline = lcd_rgb565(174, 205, 220);
    const uint16_t mercury = lcd_rgb565(55, 55, 255);
    float level = temperature_c / 50.0f;
    if (level < 0.0f) {
        level = 0.0f;
    } else if (level > 1.0f) {
        level = 1.0f;
    }
    int fill_height = (int)(level * 14.0f + 0.5f);

    lcd_draw_line(x + 2, y, x + 6, y, outline);
    lcd_draw_line(x + 1, y + 1, x + 1, y + 17, outline);
    lcd_draw_line(x + 7, y + 1, x + 7, y + 17, outline);
    lcd_draw_line(x + 2, y + 18, x + 3, y + 19, outline);
    lcd_draw_line(x + 6, y + 18, x + 5, y + 19, outline);
    lcd_draw_line(x + 3, y + 19, x + 5, y + 19, outline);
    lcd_fill_rect(x, y + 22, 9, 3, mercury);
    lcd_fill_rect(x + 1, y + 25, 7, 1, mercury);
    lcd_fill_rect(x + 2, y + 26, 5, 1, mercury);
    if (fill_height > 0) {
        lcd_fill_rect(x + 3, y + 17 - fill_height, 3, fill_height, mercury);
    }
    lcd_fill_rect(x + 2, y + 20, 5, 1, outline);
    lcd_fill_rect(x + 1, y + 21, 7, 1, outline);
}

static void lcd_draw_drop(int x, int y, uint16_t color)
{
    static const uint8_t half_width[] = {
        0, 1, 2, 3, 4, 5, 5, 5, 5, 4, 3, 2
    };
    static const uint8_t outline_half_width[] = {
        0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 5, 4, 2
    };
    const uint16_t outline = lcd_rgb565(245, 247, 255);

    for (int row = 0; row < (int)(sizeof(outline_half_width)
                                  / sizeof(outline_half_width[0])); ++row) {
        int width = 2 * outline_half_width[row] + 1;
        lcd_fill_rect(x + 6 - outline_half_width[row], y + row, width, 1,
                      outline);
    }
    for (int row = 0; row < (int)(sizeof(half_width) / sizeof(half_width[0]));
         ++row) {
        if (row > 0) {
            int inner_row = row - 1;
            int width = 2 * half_width[inner_row] + 1;
            lcd_fill_rect(x + 6 - half_width[inner_row], y + row, width, 1,
                          color);
        }
    }
}

static void lcd_draw_sun(int x, int y)
{
    const uint16_t yellow = lcd_rgb565(35, 220, 255);
    const uint16_t orange = lcd_rgb565(0, 140, 255);
    const int center_x = x + 8;
    const int center_y = y + 9;

    lcd_fill_circle(center_x, center_y, 7, orange);
    lcd_fill_circle(center_x, center_y, 5, yellow);
    lcd_draw_line(center_x, y, center_x, y + 2, orange);
    lcd_draw_line(center_x, y + 16, center_x, y + 18, orange);
    lcd_draw_line(x, center_y, x + 2, center_y, orange);
    lcd_draw_line(x + 14, center_y, x + 16, center_y, orange);
    lcd_draw_line(x + 2, y + 3, x + 4, y + 5, orange);
    lcd_draw_line(x + 12, y + 13, x + 14, y + 15, orange);
    lcd_draw_line(x + 2, y + 15, x + 4, y + 13, orange);
    lcd_draw_line(x + 12, y + 5, x + 14, y + 3, orange);
}

static void lcd_draw_soil_drop(int x, int y)
{
    const uint16_t soil = lcd_rgb565(50, 90, 145);
    lcd_draw_drop(x + 3, y, lcd_rgb565(245, 165, 65));
    lcd_draw_line(x, y + 13, x + 18, y + 13, soil);
    lcd_draw_line(x + 2, y + 16, x + 16, y + 16, soil);
    lcd_draw_line(x + 5, y + 19, x + 13, y + 19, soil);
}

static void lcd_draw_leaf_bitmap(int x, int y)
{
    static const uint32_t opaque_rows[20] = {
        0x000e0u, 0x000f8u, 0x003f0u, 0x007f8u, 0x00ff8u,
        0x01ff8u, 0x01ff8u, 0x03ff8u, 0x07ff8u, 0x07ff8u,
        0x07ff8u, 0x07ff0u, 0x0fff0u, 0x0fff0u, 0x0fff0u,
        0x0ffe0u, 0x0ff80u, 0x1fc00u, 0x0e000u, 0x0c000u,
    };
    static const uint32_t shade_rows[20] = {
        0x00000u, 0x00010u, 0x00000u, 0x00300u, 0x00000u,
        0x00a00u, 0x00a00u, 0x01200u, 0x00840u, 0x01880u,
        0x00800u, 0x00080u, 0x08300u, 0x08010u, 0x00210u,
        0x00000u, 0x00000u, 0x00000u, 0x00000u, 0x08000u,
    };
    const uint16_t leaf = lcd_rgb565(55, 175, 70);
    const uint16_t shade = lcd_rgb565(30, 95, 40);

    for (int row = 0; row < 20; ++row) {
        for (int column = 0; column < 20; ++column) {
            uint32_t pixel = 1u << column;
            if ((opaque_rows[row] & pixel) != 0) {
                uint16_t color = (shade_rows[row] & pixel) != 0 ? shade : leaf;
                lcd_fill_rect(x + column, y + row, 1, 1, color);
            }
        }
    }
}

static void lcd_draw_actuator_icons(int x, int y)
{
    const uint16_t fan_palette[16] = {
        0, 0, lcd_rgb565(75, 90, 105), lcd_rgb565(110, 125, 140),
        lcd_rgb565(155, 170, 185), lcd_rgb565(65, 115, 160),
        lcd_rgb565(100, 145, 185), lcd_rgb565(40, 95, 145),
        lcd_rgb565(45, 180, 210), lcd_rgb565(35, 60, 80),
        lcd_rgb565(210, 220, 225), lcd_rgb565(235, 170, 65),
        lcd_rgb565(90, 195, 220), lcd_rgb565(80, 105, 125),
        lcd_rgb565(55, 125, 175), lcd_rgb565(235, 240, 245),
    };
    const uint16_t sprinkler_palette[16] = {
        0, lcd_rgb565(80, 150, 220), lcd_rgb565(100, 120, 135),
        lcd_rgb565(90, 145, 75), lcd_rgb565(165, 180, 185),
        lcd_rgb565(135, 155, 170), lcd_rgb565(90, 165, 230),
        lcd_rgb565(55, 125, 190), lcd_rgb565(115, 185, 245),
        lcd_rgb565(155, 210, 250), lcd_rgb565(220, 230, 235),
        lcd_rgb565(115, 140, 155), lcd_rgb565(55, 155, 235),
        lcd_rgb565(175, 205, 220), lcd_rgb565(65, 115, 165),
        lcd_rgb565(235, 245, 250),
    };
    const uint16_t bulb_palette[16] = {
        0, lcd_rgb565(255, 255, 255), lcd_rgb565(35, 255, 255),
        lcd_rgb565(170, 170, 170), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    lcd_draw_indexed_icon(x, y, fan_icon, fan_palette);
    lcd_draw_indexed_icon(x + 18, y, sprinkler_icon, sprinkler_palette);
    int bulb_x = x + 38;
    int bulb_y = y;
    lcd_draw_indexed_icon(bulb_x, bulb_y, bulb_icon, bulb_palette);

    uint16_t sparkle = bulb_palette[1];
    lcd_draw_line(bulb_x + 8, bulb_y - 3, bulb_x + 8, bulb_y - 1, sparkle);
    lcd_draw_line(bulb_x - 2, bulb_y + 6, bulb_x - 1, bulb_y + 6, sparkle);
    lcd_draw_line(bulb_x + 16, bulb_y + 6, bulb_x + 18, bulb_y + 6, sparkle);
    lcd_draw_line(bulb_x - 2, bulb_y + 1, bulb_x - 1, bulb_y + 2, sparkle);
    lcd_draw_line(bulb_x + 16, bulb_y + 2, bulb_x + 17, bulb_y + 1, sparkle);
    lcd_draw_line(bulb_x - 2, bulb_y + 10, bulb_x - 1, bulb_y + 9, sparkle);
    lcd_draw_line(bulb_x + 16, bulb_y + 9, bulb_x + 17, bulb_y + 10, sparkle);

    uint16_t filament = bulb_palette[3];
    lcd_draw_line(bulb_x + 6, bulb_y + 5, bulb_x + 6, bulb_y + 7, filament);
    lcd_draw_line(bulb_x + 10, bulb_y + 5, bulb_x + 10, bulb_y + 7, filament);
    lcd_draw_line(bulb_x + 6, bulb_y + 7, bulb_x + 8, bulb_y + 9, filament);
    lcd_draw_line(bulb_x + 10, bulb_y + 7, bulb_x + 8, bulb_y + 9, filament);
}

static uint8_t lcd_status_glyph_row(char character, int row)
{
    static const uint8_t glyphs[][5] = {
        {0x07, 0x05, 0x05, 0x05, 0x07}, /* O */
        {0x05, 0x07, 0x07, 0x05, 0x05}, /* N */
        {0x04, 0x04, 0x04, 0x04, 0x07}, /* L */
        {0x07, 0x02, 0x02, 0x02, 0x07}, /* I */
        {0x07, 0x04, 0x06, 0x04, 0x07}, /* E */
        {0x07, 0x04, 0x06, 0x04, 0x04}, /* F */
    };
    static const char alphabet[] = "ONLIEF";
    for (int index = 0; alphabet[index] != '\0'; ++index) {
        if (alphabet[index] == character) {
            return glyphs[index][row];
        }
    }
    return 0;
}

static void lcd_draw_status_text(int x, int y, const char *text,
                                 uint16_t color)
{
    for (; *text != '\0'; ++text) {
        for (int row = 0; row < 5; ++row) {
            uint8_t pixels = lcd_status_glyph_row(*text, row);
            for (int column = 0; column < 3; ++column) {
                if ((pixels & (1U << (2 - column))) != 0) {
                    lcd_fill_rect(x + column, y + row, 1, 1, color);
                }
            }
        }
        x += 4;
    }
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
    case 'X': return GLYPH(0x63, 0x14, 0x08, 0x14, 0x63);
    case 'Z': return GLYPH(0x43, 0x45, 0x49, 0x51, 0x61);
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
    case ':': return GLYPH(0x00, 0x00, 0x22, 0x00, 0x00);
    case '-': return GLYPH(0x08, 0x08, 0x08, 0x08, 0x08);
    case '%': return GLYPH(0x63, 0x63, 0x14, 0x2A, 0x63);
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

    ESP_LOGI(TAG, "ST7735 initialized");
    return ESP_OK;
}

esp_err_t st7735_display_fill_color(uint8_t red, uint8_t green, uint8_t blue)
{
    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, lcd_rgb565(red, green, blue));
    return lcd_render();
}

esp_err_t st7735_display_show_readings(bool dht_valid, float temperature_c,
                                       float humidity, bool soil_valid,
                                       int soil_adc_raw, bool light_valid,
                                       int light_lux)
{
    const uint16_t background = lcd_rgb565(7, 18, 35);
    const uint16_t card = lcd_rgb565(17, 35, 56);
    const uint16_t white = lcd_rgb565(240, 247, 255);
    const uint16_t muted = lcd_rgb565(125, 159, 184);
    char value[24];
    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, background);
    lcd_draw_text(3, 4, "V0.1", muted, 1);
    int64_t elapsed_seconds = esp_timer_get_time() / 1000000;
    snprintf(value, sizeof(value), "%02lld:%02lld",
             (long long)((elapsed_seconds / 3600) % 100),
             (long long)((elapsed_seconds / 60) % 60));
    lcd_draw_text(3, 16, value, white, 1);
    lcd_draw_leaf_bitmap(34, 5);
    const bool mqtt_connected = false;
    lcd_draw_status_text(93, 0, mqtt_connected ? "ONLINE" : "OFFLINE",
                         mqtt_connected ? lcd_rgb565(55, 210, 65)
                                        : lcd_rgb565(255, 80, 80));
    lcd_draw_mqtt_status(125, 3, mqtt_connected);
    lcd_draw_actuator_icons(68, 14);

    lcd_fill_rect(5, 31, 118, 29, card);
    lcd_draw_text(10, 33, "TEMP", muted, 1);
    if (dht_valid) {
        snprintf(value, sizeof(value), "%.1f C", (double)temperature_c);
    } else {
        snprintf(value, sizeof(value), "--.- C");
    }
    lcd_draw_text(10, 42, value, white, 2);
    lcd_draw_thermometer(104, 32, dht_valid ? temperature_c : 0.0f);

    lcd_fill_rect(5, 63, 118, 29, card);
    lcd_draw_text(10, 65, "HUMIDITY", muted, 1);
    if (dht_valid) {
        snprintf(value, sizeof(value), "%.1f%%", (double)humidity);
    } else {
        snprintf(value, sizeof(value), "--.-%%");
    }
    lcd_draw_text(10, 74, value, lcd_rgb565(120, 225, 153), 2);
    lcd_draw_drop(103, 69, lcd_rgb565(245, 165, 65));

    lcd_fill_rect(5, 95, 118, 29, card);
    lcd_draw_text(10, 97, "SOIL MOISTURE", muted, 1);
    if (soil_valid) {
        int soil_raw = soil_adc_raw;
        if (soil_raw < 0) {
            soil_raw = 0;
        } else if (soil_raw > 4095) {
            soil_raw = 4095;
        }
        int soil_percent = soil_raw * 100 / 4095;
        snprintf(value, sizeof(value), "%d%%", soil_percent);
    } else {
        snprintf(value, sizeof(value), "ERR");
    }
    lcd_draw_text(10, 106, value, lcd_rgb565(171, 211, 120), 2);
    lcd_draw_soil_drop(101, 97);

    lcd_fill_rect(5, 127, 118, 29, card);
    lcd_draw_text(10, 129, "LUX", muted, 1);
    if (light_valid) {
        snprintf(value, sizeof(value), "%d", light_lux);
    } else {
        snprintf(value, sizeof(value), "ERR");
    }
    lcd_draw_text(10, 138, value, lcd_rgb565(255, 196, 92), 2);
    lcd_draw_sun(101, 132);

    return lcd_render();
}

esp_err_t st7735_display_show_dht11(float temperature_c, float humidity)
{
    const uint16_t background = lcd_rgb565(7, 18, 35);
    const uint16_t card = lcd_rgb565(17, 35, 56);
    const uint16_t white = lcd_rgb565(240, 247, 255);
    const uint16_t muted = lcd_rgb565(125, 159, 184);
    char value[24];

    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, background);
    lcd_draw_text(8, 7, "DHT11", lcd_rgb565(42, 220, 220), 2);

    lcd_fill_rect(5, 40, 118, 47, card);
    lcd_draw_text(10, 45, "TEMP", muted, 1);
    snprintf(value, sizeof(value), "%.1f C", (double)temperature_c);
    lcd_draw_text(10, 59, value, white, 2);

    lcd_fill_rect(5, 94, 118, 56, card);
    lcd_draw_text(10, 99, "HUMIDITY", muted, 1);
    snprintf(value, sizeof(value), "%.1f%%", (double)humidity);
    lcd_draw_text(10, 116, value, lcd_rgb565(120, 225, 153), 2);

    return lcd_render();
}

esp_err_t st7735_display_show_dht11_error(void)
{
    const uint16_t background = lcd_rgb565(7, 18, 35);
    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, background);
    lcd_draw_text(8, 7, "DHT11", lcd_rgb565(42, 220, 220), 2);
    lcd_draw_text(8, 54, "SENSOR", lcd_rgb565(255, 105, 105), 2);
    lcd_draw_text(8, 72, "ERROR", lcd_rgb565(255, 105, 105), 2);
    return lcd_render();
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
    lcd_draw_thermometer(11, 50, sensor->temperature_c);
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
    lcd_draw_text(8, 104, "CHECK I2C", lcd_rgb565(240, 247, 255), 1);
    return lcd_render();
}
