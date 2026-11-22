// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_ui.h"
#include "nrf5_via_ui.h"
#include "oled_driver.h"
#include "oled_text_font.h"
#include "traditional_clock_font.h"
#include "outputselect.h"
#include "adc.h"
#include <stdio.h>
#include <string.h>
#ifdef NRF5_CLOCK_ENABLE
#    include "nrf5_clock.h"
#endif
#ifdef NRF5_OLED_TITLE_BITMAP_HEADER
#    include NRF5_OLED_TITLE_BITMAP_HEADER
#endif
#ifdef NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
#    include NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
#endif
#ifndef NRF5_OLED_TITLE
#    define NRF5_OLED_TITLE "Qmknrf5"
#endif
#ifndef NRF5_OLED_LAYER_NAMES
#    define NRF5_OLED_LAYER_NAMES {"BASE"}
#endif
#ifndef NRF5_OLED_BATTERY_AVAILABLE
#    define NRF5_OLED_BATTERY_AVAILABLE 1
#endif
static const char *const names[] = NRF5_OLED_LAYER_NAMES;
static uint8_t frame[OLED_DISPLAY_WIDTH * OLED_DISPLAY_HEIGHT / 8];
static void pixel(uint16_t x, uint16_t y) {
    if (x < OLED_DISPLAY_WIDTH && y < OLED_DISPLAY_HEIGHT) frame[(y / 8) * OLED_DISPLAY_WIDTH + x] |= 1 << (y % 8);
}
static void bitmap(const uint8_t *p, uint16_t width, uint16_t height, uint16_t x, uint16_t y, uint8_t scale) {
    uint16_t stride = (width + 7) / 8;
    for (uint16_t py = 0; py < height; py++) for (uint16_t px = 0; px < width; px++) {
        if (!(p[py * stride + px / 8] & (0x80 >> (px % 8)))) continue;
        for (uint8_t dy = 0; dy < scale; dy++) for (uint8_t dx = 0; dx < scale; dx++) pixel(x + px * scale + dx, y + py * scale + dy);
    }
}
static void text(const char *s, uint16_t x, uint16_t y, uint8_t scale) {
    while (*s && x < OLED_DISPLAY_WIDTH) {
        uint8_t c = *s++;
        const uint8_t *g = oled_text_font[(c >= 32 && c <= 126 ? c : '?') - 32];
        for (uint8_t px = 0; px < 6; px++) for (uint8_t py = 0; py < 8; py++) if (g[px] & (1 << py))
            for (uint8_t dy = 0; dy < scale; dy++) for (uint8_t dx = 0; dx < scale; dx++) pixel(x + px * scale + dx, y + py * scale + dy);
        x += 6 * scale;
    }
}
void nrf5_ui_oled_render(void) {
    const uint8_t *c = nrf5_ui_config(); memset(frame,0,sizeof(frame));
    uint8_t layer = get_highest_layer(layer_state | default_layer_state);
    uint8_t output = where_to_send();
    const char *host = output == OUTPUT_USB ? "USB" : output == OUTPUT_BLUETOOTH ? "BLE" : "AUTO";
    uint8_t hour = 0, minute = 0, second = 0; bool valid = false;
#ifdef NRF5_CLOCK_ENABLE
    valid = nrf5_clock_read(&hour,&minute,&second);
#endif
    for (uint8_t id = 0; id < NRF5_UI_WIDGETS; id++) {
        const uint8_t *w = c + 16 + id * 4; if (!w[3]) continue;
        uint16_t x = w[0], y = w[1]; uint8_t scale = w[2]; char s[24];
        switch (id) {
            case 0:
                if (c[1] & 2) bitmap(c + NRF5_UI_LOGO,c[6],c[7],x,y,scale);
#ifdef NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
                else bitmap(nrf5_oled_title_prefix_bitmap,NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH,NRF5_OLED_TITLE_PREFIX_BITMAP_HEIGHT,x,y,scale);
#endif
                break;
            case 1:
                if (c[1] & 1) bitmap(c + NRF5_UI_TITLE,c[4],c[5],x,y,scale);
#ifdef NRF5_OLED_TITLE_BITMAP_HEADER
                else bitmap(nrf5_oled_title_bitmap,NRF5_OLED_TITLE_BITMAP_WIDTH,NRF5_OLED_TITLE_BITMAP_HEIGHT,x,y,scale);
#else
                else text(NRF5_OLED_TITLE,x,y,scale);
#endif
                break;
            case 2: {
                uint8_t ids[4] = {18,19,20,0}, count = 3;
#ifdef NRF5_CLOCK_ENABLE
                if (valid) { uint8_t quarter = nrf5_clock_quarter(minute); ids[0] = nrf5_clock_branch(hour); ids[1] = 12 + nrf5_clock_is_zheng(hour); ids[2] = quarter ? 13 + quarter : 12; ids[3] = 17; count = 4; }
#endif
                for (uint8_t i = 0; i < count; i++) bitmap((c[1] & 4) ? c + NRF5_UI_FONT + ids[i] * 32 : traditional_clock_glyphs[ids[i]],16,16,x + i * 16 * scale,y,scale);
                break;
            }
            case 3:
                if (valid) snprintf(s,sizeof(s),"%02u:%02u",hour,minute); else snprintf(s,sizeof(s),"--:--");
                text(s,x,y,scale); break;
            case 4:
#if NRF5_OLED_BATTERY_AVAILABLE
                snprintf(s,sizeof(s),"%u%%",(unsigned)get_battery_level());
#else
                snprintf(s,sizeof(s),"--%%");
#endif
                text(s,x,y,scale); break;
            case 5: text(host,x,y,scale); break;
            case 6:
                snprintf(s,sizeof(s),"L%u",layer); text(layer < ARRAY_SIZE(names) ? names[layer] : s,x,y,scale); break;
        }
    }
    for (uint16_t i = 0; i < sizeof(frame); i++) oled_write_raw_byte(frame[i],i);
}
