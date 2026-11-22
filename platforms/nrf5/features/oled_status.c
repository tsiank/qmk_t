// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_ui.h"
#include "oled_driver.h"
#include "adc.h"
#include "outputselect.h"
#ifdef NRF5_ESB_ENABLE
#    include "nrf5_esb.h"
#endif
#include <stdio.h>
#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#endif
#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix.h"
#endif
#ifdef NRF5_CONFIG_ENABLE
#    include "nrf5_midi_typing.h"
#endif

#ifndef NRF5_OLED_TITLE
#    define NRF5_OLED_TITLE "Qmknrf5"
#endif
#ifndef NRF5_OLED_BATTERY_AVAILABLE
#    define NRF5_OLED_BATTERY_AVAILABLE 1
#endif
#ifndef NRF5_OLED_LAYER_NAMES
#    define NRF5_OLED_LAYER_NAMES {"BASE"}
#endif
static const char *const layer_names[] = NRF5_OLED_LAYER_NAMES;

#ifdef NRF5_OLED_TITLE_BITMAP_HEADER
#    include NRF5_OLED_TITLE_BITMAP_HEADER
#    ifdef NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
#        include NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
#        define NRF5_OLED_TITLE_OFFSET NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH
_Static_assert(NRF5_OLED_TITLE_PREFIX_BITMAP_HEIGHT == 16 && NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH > 0,
               "OLED title prefix must have positive width and be 16 pixels tall");
_Static_assert(sizeof(nrf5_oled_title_prefix_bitmap) == ((NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH + 7) / 8) * 16,
               "OLED title prefix size does not match dimensions");
#    else
#        define NRF5_OLED_TITLE_OFFSET 0
#    endif
#    define NRF5_OLED_TITLE_RENDER_WIDTH (NRF5_OLED_TITLE_BITMAP_WIDTH + NRF5_OLED_TITLE_OFFSET)
_Static_assert(NRF5_OLED_TITLE_BITMAP_HEIGHT == 16, "OLED title bitmap must be 16 pixels tall");
_Static_assert(NRF5_OLED_TITLE_BITMAP_WIDTH > 0 && (NRF5_OLED_TITLE_RENDER_WIDTH + OLED_FONT_WIDTH - 1) / OLED_FONT_WIDTH + 6 <= OLED_DISPLAY_WIDTH / OLED_FONT_WIDTH,
               "OLED title must leave at least six text columns for status");
_Static_assert(OLED_DISPLAY_HEIGHT >= 32, "Bitmap title needs a display at least 32 pixels tall");
_Static_assert(sizeof(nrf5_oled_title_bitmap) == ((NRF5_OLED_TITLE_BITMAP_WIDTH + 7) / 8) * NRF5_OLED_TITLE_BITMAP_HEIGHT,
               "OLED title bitmap size does not match dimensions");

static void draw_title_bitmap(void) {
    // PCtoLCD row-major/MSB pixels -> SSD1306 vertical LSB page bytes.
    for (uint8_t page = 0; page < 2; page++) {
        for (uint16_t x = 0; x < NRF5_OLED_TITLE_RENDER_WIDTH; x++) {
            uint8_t pixels = 0;
            uint16_t source_x = x;
            uint16_t stride = (NRF5_OLED_TITLE_BITMAP_WIDTH + 7) / 8;
            const uint8_t *bitmap = nrf5_oled_title_bitmap;
#    ifdef NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
            if (x < NRF5_OLED_TITLE_OFFSET) {
                stride = (NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH + 7) / 8;
                bitmap = nrf5_oled_title_prefix_bitmap;
            } else source_x -= NRF5_OLED_TITLE_OFFSET;
#    endif
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint16_t offset = (page * 8 + bit) * stride + source_x / 8;
                if (bitmap[offset] & (0x80 >> (source_x % 8))) pixels |= 1 << bit;
            }
            oled_write_raw_byte((char)pixels, page * OLED_DISPLAY_WIDTH + x);
        }
    }
}
#endif

#ifdef NRF5_CLOCK_ENABLE
#    include "nrf5_clock.h"
#    include "traditional_clock_font.h"
#    include "oled_text_font.h"
_Static_assert(OLED_DISPLAY_WIDTH >= 128 && OLED_DISPLAY_HEIGHT >= 32, "Traditional clock needs an 128x32 or larger display");
// Draw printable ASCII into a 16-pixel strip. Scale 1 is 6x8, scale 2 is 12x16.
static void draw_ascii(uint8_t pixels[2][OLED_DISPLAY_WIDTH], uint16_t x, uint8_t y, const char *text, uint8_t scale) {
    if (y + 8 * scale > 16) return;
    for (uint16_t index = 0; text[index] && x + 6 * scale <= OLED_DISPLAY_WIDTH; index++, x += 6 * scale) {
        uint8_t c = (uint8_t)text[index];
        const uint8_t *glyph = oled_text_font[(c >= 32 && c <= 126 ? c : '?') - 32];
        for (uint8_t gx = 0; gx < 6; gx++) for (uint8_t bit = 0; bit < 8; bit++) {
            if (!(glyph[gx] & (1 << bit))) continue;
            for (uint8_t dx = 0; dx < scale; dx++) for (uint8_t dy = 0; dy < scale; dy++) {
                uint8_t py = y + bit * scale + dy;
                pixels[py / 8][x + gx * scale + dx] |= 1 << (py % 8);
            }
        }
    }
}
static void draw_clock_header(uint8_t layer) {
    uint8_t pixels[2][OLED_DISPLAY_WIDTH] = {{0}};
#ifdef NRF5_OLED_TITLE_BITMAP_HEADER
    _Static_assert(NRF5_OLED_TITLE_RENDER_WIDTH + 48 <= OLED_DISPLAY_WIDTH, "Clock title must leave four large layer characters");
    draw_title_bitmap();
    const uint16_t start = NRF5_OLED_TITLE_RENDER_WIDTH;
    const uint16_t label_x = start > 68 ? start : 68;
#else
    const uint16_t start = 0;
    const uint16_t label_x = 68;
    char title[11];
    snprintf(title, sizeof(title), "%s", NRF5_OLED_TITLE);
    draw_ascii(pixels, 0, 0, title, 1);
#endif
    char fallback[5];
    snprintf(fallback, sizeof(fallback), "L%u", layer);
    const char *label = layer < ARRAY_SIZE(layer_names) ? layer_names[layer] : fallback;
    draw_ascii(pixels, label_x, 0, label, 2);
    for (uint8_t page = 0; page < 2; page++) for (uint16_t x = start; x < OLED_DISPLAY_WIDTH; x++)
        oled_write_raw_byte((char)pixels[page][x], page * OLED_DISPLAY_WIDTH + x);
}
static void draw_traditional_time(uint8_t hour, uint8_t minute, bool valid, const char *host, uint8_t layer) {
    uint8_t glyphs[4] = {18, 19, 20}; // 待校时
    uint8_t count = 3;
    if (valid) {
        glyphs[0] = nrf5_clock_branch(hour);
        glyphs[1] = 12 + nrf5_clock_is_zheng(hour);
        uint8_t quarter = nrf5_clock_quarter(minute);
        glyphs[2] = quarter == 0 ? 12 : 13 + quarter;
        glyphs[3] = 17;
        count = 4;
    }
    uint8_t pixels[2][OLED_DISPLAY_WIDTH] = {{0}};
    const uint16_t left = 0;
    for (uint8_t index = 0; index < count; index++) {
        const uint8_t *glyph = traditional_clock_glyphs[glyphs[index]];
        for (uint8_t page = 0; page < 2; page++) for (uint8_t x = 0; x < 16; x++) {
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (glyph[(page * 8 + bit) * 2 + x / 8] & (0x80 >> (x % 8)))
                    pixels[page][left + index * 16 + x] |= 1 << bit;
            }
        }
    }
    // Keep HH:MM small; the right half has two 6x8 rows for time/battery
    // and connection/layer index. Render directly to avoid text-page wrapping.
    char time[8], text[24];
    if (valid) snprintf(time, sizeof(time), "%02u:%02u", hour, minute);
    else snprintf(time, sizeof(time), "--:--");
#    if NRF5_OLED_BATTERY_AVAILABLE
    snprintf(text, sizeof(text), "%s %u%%", time, (unsigned)get_battery_level());
#    else
    snprintf(text, sizeof(text), "%s --%%", time);
#    endif
    draw_ascii(pixels, 68, 0, text, 1);
    snprintf(text, sizeof(text), "%s L%u", host, layer);
    draw_ascii(pixels, 68, 8, text, 1);
    for (uint8_t page = 0; page < 2; page++) for (uint16_t x = 0; x < OLED_DISPLAY_WIDTH; x++)
        oled_write_raw_byte((char)pixels[page][x], (page + 2) * OLED_DISPLAY_WIDTH + x);
}
#endif

bool nrf5_oled_task(void) {
    if (!oled_task_user()) return false;
#ifdef NRF5_VIA_UI_ENABLE
    void nrf5_ui_oled_render(void);
    nrf5_ui_oled_render();
    return false;
#endif
    // Leave one text column free: QMK automatically advances on a full row,
    // and a following write_ln would clear the next row (including artwork).
#ifndef NRF5_CLOCK_ENABLE
    char text[OLED_DISPLAY_WIDTH / OLED_FONT_WIDTH];
#endif
    uint8_t layer = get_highest_layer(layer_state | default_layer_state);
    uint8_t output = where_to_send();
    const char *host = output == OUTPUT_USB ? "USB" : output == OUTPUT_BLUETOOTH ? "BLE" : "AUTO";
#ifdef NRF5_ESB_ENABLE
    if (output == OUTPUT_NRF5_ESB) host = nrf5_esb_connected() ? "RF+" : "RF-";
#endif
#ifdef NRF5_CLOCK_ENABLE
    uint8_t hour = 0, minute = 0, second = 0;
    bool valid = nrf5_clock_read(&hour, &minute, &second);
    draw_clock_header(layer);
    draw_traditional_time(hour, minute, valid, host, layer);
    return false;
#else
#ifdef NRF5_OLED_TITLE_BITMAP_HEADER
    draw_title_bitmap();
    const uint8_t status_column = (NRF5_OLED_TITLE_RENDER_WIDTH + OLED_FONT_WIDTH - 1) / OLED_FONT_WIDTH;
    const uint8_t status_width = OLED_DISPLAY_WIDTH / OLED_FONT_WIDTH - status_column;
    oled_set_cursor(status_column, 0);
    if (layer < ARRAY_SIZE(layer_names)) snprintf(text, sizeof(text), "%s", layer_names[layer]);
    else snprintf(text, sizeof(text), "L%u", layer);
    text[status_width - 1] = '\0';
    oled_write_ln(text, false);
    oled_set_cursor(status_column, 1);
    oled_write_ln(host, false);
    // A 16-pixel title occupies two text rows. Retain lighting and lock state
    // below it; the four-row text-only layout still includes battery status.
    oled_set_cursor(0, 2);
#else
    oled_set_cursor(0, 0);
    if (layer < ARRAY_SIZE(layer_names)) {
        snprintf(text, sizeof(text), "%s %-4s %-4s", NRF5_OLED_TITLE, layer_names[layer], host);
    } else {
        snprintf(text, sizeof(text), "%s L%-3u %-4s", NRF5_OLED_TITLE, layer, host);
    }
    oled_write_ln(text, false);
    snprintf(text, sizeof(text), "BAT %4umV %3u%%   ", (unsigned)get_vcc(), (unsigned)get_battery_level());
    oled_write_ln(text, false);
#endif
#ifdef RGBLIGHT_ENABLE
    snprintf(text, sizeof(text), "RGB %-3s M%-2u V%-3u   ", rgblight_is_enabled() ? "ON" : "OFF", rgblight_get_mode(), rgblight_get_val());
#elif defined(RGB_MATRIX_ENABLE)
    snprintf(text, sizeof(text), "RGB %-3s M%-2u V%-3u   ", rgb_matrix_is_enabled() ? "ON" : "OFF", rgb_matrix_get_mode(), rgb_matrix_get_val());
#else
    snprintf(text, sizeof(text), "RGB --              ");
#endif
    oled_write_ln(text, false);
    led_t leds = host_keyboard_led_state();
    bool midi = false;
#ifdef NRF5_CONFIG_ENABLE
    midi = nrf5_midi_typing_enabled();
#endif
    snprintf(text, sizeof(text), "NUM:%u CAPS:%u MIDI:%u ", leds.num_lock, leds.caps_lock, midi);
    oled_write_ln(text, false);
    return false;
#endif
}
