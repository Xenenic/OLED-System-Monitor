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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "ssd1309.h"

#include "tusb.h"

#define JSON_BUFFER_SIZE 256

static char json_buffer[JSON_BUFFER_SIZE];
static int json_idx = 0;
static bool in_json = false;
static int brace_count = 0;

static void reset_json_state(void) {
    in_json = false;
    json_idx = 0;
    brace_count = 0;
}

void draw_bar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, float percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Outline
    ssd1309_draw_rect(x, y, w, h, false);

    // Fill
    uint8_t fill_w = (uint8_t)((w - 4) * (percent / 100.0f));
    if (fill_w > 0) {
        ssd1309_draw_rect(x + 2, y + 2, fill_w, h - 4, true);
    }
}

void render_stats(float cpu, float ram, float swap_or_page, const char *swap_label, float l1, float l5, float l15, const char *uptime) {
    ssd1309_clear();

    char buf[32];
    
    // Load Average at the top
    snprintf(buf, sizeof(buf), "LOAD: %.2f %.2f %.2f", l1, l5, l15);
    ssd1309_draw_string(0, 2, buf);

    // Uptime below it
    snprintf(buf, sizeof(buf), "UPTIME: %s", uptime);
    ssd1309_draw_string(0, 12, buf);

    // CPU Bar
    ssd1309_draw_string(0, 26, "CPU");
    draw_bar(28, 26, 78, 9, cpu);
    snprintf(buf, sizeof(buf), "%3.0f%%", cpu);
    ssd1309_draw_string(104, 27, buf);

    // RAM Bar
    ssd1309_draw_string(0, 39, "RAM");
    draw_bar(28, 39, 78, 9, ram);
    snprintf(buf, sizeof(buf), "%3.0f%%", ram);
    ssd1309_draw_string(104, 40, buf);

    // SWAP/PAGE Bar
    ssd1309_draw_string(0, 52, swap_label);
    draw_bar(28, 52, 78, 9, swap_or_page);
    snprintf(buf, sizeof(buf), "%3.0f%%", swap_or_page);
    ssd1309_draw_string(104, 53, buf);

    ssd1309_show();
}

static bool json_has_key(const char *json, const char *key) {
    return strstr(json, key) != NULL;
}

float get_json_float(const char *json, const char *key) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    return strtof(p, NULL);
}

void get_json_string(const char *json, const char *key, char *dest, int max_len) {
    const char *p = strstr(json, key);
    if (!p) {
        strncpy(dest, "N/A", max_len);
        return;
    }
    p = strchr(p, ':');
    if (!p) {
        strncpy(dest, "N/A", max_len);
        return;
    }
    p = strchr(p, '\"');
    if (!p) {
        strncpy(dest, "N/A", max_len);
        return;
    }
    p++;
    const char *end = strchr(p, '\"');
    if (!end) {
        strncpy(dest, "N/A", max_len);
        return;
    }
    int len = end - p;
    if (len >= max_len) len = max_len - 1;
    strncpy(dest, p, len);
    dest[len] = '\0';
}

void get_json_load(const char *json, float *l1, float *l5, float *l15) {
    const char *p = strstr(json, "\"load\"");
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;
    char *endptr;
    *l1 = strtof(p, &endptr);
    p = strchr(endptr, ',');
    if (p) {
        p++;
        *l5 = strtof(p, &endptr);
        p = strchr(endptr, ',');
        if (p) {
            p++;
            *l15 = strtof(p, &endptr);
        }
    }
}

void parse_and_display(const char *json) {
    float cpu = get_json_float(json, "\"cpu\"");
    float ram = get_json_float(json, "\"ram\"");
    bool have_page = json_has_key(json, "\"page\"");
    float swap_or_page = have_page ? get_json_float(json, "\"page\"")
                                   : get_json_float(json, "\"swap\"");
    float l1 = 0, l5 = 0, l15 = 0;
    get_json_load(json, &l1, &l5, &l15);
    char uptime[32];
    get_json_string(json, "\"uptime\"", uptime, sizeof(uptime));

    // Debug to USB serial to confirm frames are received and parsed.
    printf("RX JSON: %s\n", json);
    printf("Parsed cpu=%.1f ram=%.1f %s=%.1f load=[%.2f %.2f %.2f] up=%s\n",
           cpu, ram, have_page ? "page" : "swap", swap_or_page, l1, l5, l15, uptime);

    render_stats(cpu, ram, swap_or_page, have_page ? "PAGE" : "SWAP", l1, l5, l15, uptime);
}

void handle_char(char c) {
    // If we are not currently inside a JSON frame, ignore everything until
    // we see the start of an object.
    if (!in_json) {
        if (c != '{') return;
        in_json = true;
        json_idx = 0;
        brace_count = 0;
    }

    // In-frame: hard reset on overflow so we can re-sync on the next '{'.
    if (json_idx >= JSON_BUFFER_SIZE - 1) {
        reset_json_state();
        return;
    }

    json_buffer[json_idx++] = c;

    if (c == '{') {
        brace_count++;
    } else if (c == '}') {
        if (brace_count == 0) {
            // Invalid stream (e.g., missed '{'); reset so we can re-sync.
            reset_json_state();
            return;
        }
        brace_count--;
    }

    // If the outermost object just closed, dispatch it.
    if (brace_count == 0) {
        json_buffer[json_idx] = '\0';
        parse_and_display(json_buffer);
        reset_json_state();
    }
}

int main() {
    stdio_init_all();
    
    // Give some time for USB serial to connect
    sleep_ms(2000);

    // On some setups, servicing TinyUSB explicitly helps RX reliability.
    // (Doesn't hurt even if interrupts are also enabled.)
    for (int i = 0; i < 50 && !stdio_usb_connected(); i++) {
        tud_task();
        sleep_ms(20);
    }
    
    ssd1309_init();
    ssd1309_clear();
    ssd1309_draw_string(0, 0, "System Monitor v1");
    ssd1309_draw_string(0, 12, "Waiting for JSON v1...");
    ssd1309_show();

    printf("System Monitor v1 ready. Send JSON now.\n");

    while (true) {
        // Keep TinyUSB serviced so CDC RX continues to flow.
        tud_task();

        int c = getchar_timeout_us(100);
        if (c != PICO_ERROR_TIMEOUT) {
            // Filter out non-printable bytes to avoid poisoning the frame
            // with line noise or NULs from some terminals.
            char ch = (char)c;
            if (ch == '\r' || ch == '\n' || ch == '\t' || isprint((unsigned char)ch)) {
                handle_char(ch);
            }
        }
    }

    return 0;
}
