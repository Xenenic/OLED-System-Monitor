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
void ssd1309_clear();
void ssd1309_show();
void ssd1309_draw_pixel(uint8_t x, uint8_t y, bool on);
void ssd1309_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void ssd1309_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool fill);
void ssd1309_draw_char(uint8_t x, uint8_t y, char c);
void ssd1309_draw_string(uint8_t x, uint8_t y, const char *str);
void ssd1309_scroll_up();

#endif // SSD1309_H
