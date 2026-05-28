/*
 * Copyright (c) 2026 Xenenic
 *
 * This work is licensed under the Creative Commons Attribution-NonCommercial 4.0
 * International License. To view a copy of this license, visit
 * https://creativecommons.org/licenses/by-nc/4.0/ or send a letter to
 * Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 *
 * You are free to:
 *  - Share: Copy and redistribute the material in any medium or format
 *  - Adapt: Remix, transform, and build upon the material
 *
 * Under the following terms:
 *  - Attribution: You must give appropriate credit, provide a link to the license,
 *    and indicate if changes were made.
 *  - NonCommercial: You may not use this material for commercial purposes.
 */

#include "ssd1309.h"
#include "font.h"
#include <string.h>
#include <stdlib.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"

static uint8_t buffer[SSD1309_WIDTH * SSD1309_HEIGHT / 8];

static uint8_t last_contrast = 0xCF;

static void ssd1309_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(I2C_PORT, SSD1309_ADDR, buf, 2, false);
}

static void ssd1309_write_data(uint8_t *data, size_t len) {
    uint8_t *buf = malloc(len + 1);
    buf[0] = 0x40;
    memcpy(buf + 1, data, len);
    i2c_write_blocking(I2C_PORT, SSD1309_ADDR, buf, len + 1, false);
    free(buf);
}

void ssd1309_init() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);

    sleep_ms(100);

    ssd1309_write_cmd(0xAE); // Display off
    ssd1309_write_cmd(0xD5); // Set display clock divide ratio
    ssd1309_write_cmd(0x80);
    ssd1309_write_cmd(0xA8); // Set multiplex ratio
    ssd1309_write_cmd(0x3F);
    ssd1309_write_cmd(0xD3); // Set display offset
    ssd1309_write_cmd(0x00);
    ssd1309_write_cmd(0x40); // Set start line
    ssd1309_write_cmd(0x8D); // Charge pump
    ssd1309_write_cmd(0x14);
    ssd1309_write_cmd(0x20); // Memory addressing mode
    ssd1309_write_cmd(0x00); // Horizontal addressing mode
    ssd1309_write_cmd(0xA1); // Set segment re-map
    ssd1309_write_cmd(0xC8); // Set COM output scan direction
    ssd1309_write_cmd(0xDA); // Set COM pins hardware configuration
    ssd1309_write_cmd(0x12);
    ssd1309_write_cmd(0x81); // Set contrast control
    ssd1309_write_cmd(0xCF);
    last_contrast = 0xCF;
    ssd1309_write_cmd(0xD9); // Set pre-charge period
    ssd1309_write_cmd(0xF1);
    ssd1309_write_cmd(0xDB); // Set VCOMH deselect level
    ssd1309_write_cmd(0x40);
    ssd1309_write_cmd(0xA4); // Entire display on
    ssd1309_write_cmd(0xA6); // Set normal display
    ssd1309_write_cmd(0xAF); // Display on

    ssd1309_clear();
    ssd1309_show();
}

void ssd1309_set_contrast(uint8_t contrast) {
    if (contrast == last_contrast) return;
    ssd1309_write_cmd(0x81);
    ssd1309_write_cmd(contrast);
    last_contrast = contrast;
}

void ssd1309_clear() {
    memset(buffer, 0, sizeof(buffer));
}

void ssd1309_show() {
    ssd1309_write_cmd(0x21); // Column address
    ssd1309_write_cmd(0);
    ssd1309_write_cmd(SSD1309_WIDTH - 1);
    ssd1309_write_cmd(0x22); // Page address
    ssd1309_write_cmd(0);
    ssd1309_write_cmd((SSD1309_HEIGHT / 8) - 1);

    ssd1309_write_data(buffer, sizeof(buffer));
}

void ssd1309_draw_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= SSD1309_WIDTH || y >= SSD1309_HEIGHT) return;
    if (on) {
        buffer[x + (y / 8) * SSD1309_WIDTH] |= (1 << (y % 8));
    } else {
        buffer[x + (y / 8) * SSD1309_WIDTH] &= ~(1 << (y % 8));
    }
}

void ssd1309_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    int e2;

    for (;;) {
        ssd1309_draw_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

void ssd1309_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool fill) {
    if (fill) {
        for (int i = x; i < x + w; i++) {
            for (int j = y; j < y + h; j++) {
                ssd1309_draw_pixel(i, j, true);
            }
        }
    } else {
        ssd1309_draw_line(x, y, x + w - 1, y);
        ssd1309_draw_line(x, y + h - 1, x + w - 1, y + h - 1);
        ssd1309_draw_line(x, y, x, y + h - 1);
        ssd1309_draw_line(x + w - 1, y, x + w - 1, y + h - 1);
    }
}

void ssd1309_draw_char(uint8_t x, uint8_t y, char c) {
    if (c < 32 || c > 126) return;
    c -= 32;
    for (int i = 0; i < 5; i++) {
        uint8_t line = font_5x7[(int)c][i];
        for (int j = 0; j < 8; j++) {
            if (line & (1 << j)) {
                int px = x + i;
                int py = y + j;
                if (px < SSD1309_WIDTH && py < SSD1309_HEIGHT) {
                    buffer[px + (py / 8) * SSD1309_WIDTH] |= (1 << (py % 8));
                }
            }
        }
    }
}

void ssd1309_draw_string(uint8_t x, uint8_t y, const char *str) {
    while (*str) {
        ssd1309_draw_char(x, y, *str++);
        x += 6;
        if (x + 6 > SSD1309_WIDTH) {
            x = 0;
            y += 8;
        }
        if (y + 8 > SSD1309_HEIGHT) break;
    }
}

void ssd1309_scroll_up() {
    memmove(buffer, buffer + SSD1309_WIDTH, sizeof(buffer) - SSD1309_WIDTH);
    memset(buffer + sizeof(buffer) - SSD1309_WIDTH, 0, SSD1309_WIDTH);
}
