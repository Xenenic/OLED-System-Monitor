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

#ifndef SSD1309_H
#define SSD1309_H

#include <stdint.h>
#include "hardware/i2c.h"

#define SSD1309_WIDTH  128
#define SSD1309_HEIGHT 64

// I2C definitions
#define I2C_PORT i2c0
#define PIN_SDA  4
#define PIN_SCL  5
#define SSD1309_ADDR 0x3C

void ssd1309_init();
void ssd1309_set_contrast(uint8_t contrast);
void ssd1309_clear();
void ssd1309_show();
void ssd1309_draw_pixel(uint8_t x, uint8_t y, bool on);
void ssd1309_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void ssd1309_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool fill);
void ssd1309_draw_char(uint8_t x, uint8_t y, char c);
void ssd1309_draw_string(uint8_t x, uint8_t y, const char *str);
void ssd1309_scroll_up();

#endif // SSD1309_H
