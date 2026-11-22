// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_ui.h"
#include "nrf5_via_ui.h"
#include "eeprom.h"
#include "eeprom_nrf.h"
#include "encoder.h"
#include "oled_driver.h"
#include <string.h>
#include "traditional_clock_font.h"
#ifdef NRF5_OLED_TITLE_BITMAP_HEADER
#    include NRF5_OLED_TITLE_BITMAP_HEADER
_Static_assert(NRF5_OLED_TITLE_BITMAP_WIDTH <= 128 && NRF5_OLED_TITLE_BITMAP_HEIGHT <= 32 && sizeof(nrf5_oled_title_bitmap) <= 512, "Title exceeds upload slot");
#endif
#ifdef NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
#    include NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
_Static_assert(NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH <= 64 && NRF5_OLED_TITLE_PREFIX_BITMAP_HEIGHT <= 32 && sizeof(nrf5_oled_title_prefix_bitmap) <= 256, "Logo exceeds upload slot");
#endif
#ifndef NRF5_UI_EEPROM_ADDR
#    error Define NRF5_UI_EEPROM_ADDR after the existing VIA keymap/macro region
#endif
_Static_assert(NRF5_UI_EEPROM_ADDR > DYNAMIC_KEYMAP_EEPROM_MAX_ADDR, "UI overlaps VIA EEPROM");
_Static_assert(NRF5_UI_EEPROM_ADDR + NRF5_UI_SIZE <= EEPROM_SIZE, "UI exceeds EEPROM capacity");
_Static_assert(FDS_VIRTUAL_PAGES >= 5, "Large UI EEPROM needs five FDS pages for peer records and GC");
_Static_assert(NUM_ENCODERS <= NRF5_UI_ENCODERS && DYNAMIC_KEYMAP_LAYER_COUNT <= NRF5_UI_LAYERS, "Increase UI protocol capacities");
_Static_assert(OLED_DISPLAY_WIDTH == 128 && (OLED_DISPLAY_HEIGHT == 32 || OLED_DISPLAY_HEIGHT == 64), "UI protocol supports 128x32 and 128x64 displays");
static uint8_t active[NRF5_UI_SIZE], draft[NRF5_UI_SIZE];
static bool loaded;
static uint16_t session, next_session;
static uint8_t owner;
static uint32_t session_activity;
static uint32_t crc32(const uint8_t *p, uint16_t n) {
    uint32_t c = 0xffffffff;
    while (n--) { c ^= *p++; for (uint8_t b = 0; b < 8; b++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); }
    return ~c;
}
static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static void seal(uint8_t *p) { uint32_t c = crc32(p, NRF5_UI_CRC); for (uint8_t i = 0; i < 4; i++) p[NRF5_UI_CRC + i] = c >> (8 * i); }
static void defaults(uint8_t *p) {
    memset(p, 0, NRF5_UI_SIZE); p[0] = 1;
    const uint8_t widgets[][4] = {{0,0,1,1},{16,0,1,1},{0,16,1,1},{68,16,1,1},{104,16,1,1},{68,24,1,1},{80,0,2,1}};
    memcpy(p + 16, widgets, sizeof(widgets));
    for (uint8_t i = 0; i < NRF5_UI_ENCODERS * NRF5_UI_LAYERS; i++) p[48 + i * 5] = 255;
    p[1] = 4; memcpy(p + NRF5_UI_FONT, traditional_clock_glyphs, sizeof(traditional_clock_glyphs));
#ifdef NRF5_OLED_TITLE_BITMAP_HEADER
    p[1] |= 1; p[4] = NRF5_OLED_TITLE_BITMAP_WIDTH; p[5] = NRF5_OLED_TITLE_BITMAP_HEIGHT;
    memcpy(p + NRF5_UI_TITLE,nrf5_oled_title_bitmap,sizeof(nrf5_oled_title_bitmap));
#endif
#ifdef NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
    p[1] |= 2; p[6] = NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH; p[7] = NRF5_OLED_TITLE_PREFIX_BITMAP_HEIGHT;
    memcpy(p + NRF5_UI_LOGO,nrf5_oled_title_prefix_bitmap,sizeof(nrf5_oled_title_prefix_bitmap));
#endif
    seal(p);
}
static bool valid(const uint8_t *p) {
    if (p[0] != 1 || (p[1] & ~7) || p[2] > 2 || p[3]) return false;
    if ((p[1] & 1) && (!p[4] || p[4] > 128 || !p[5] || p[5] > 32 || ((p[4] + 7) / 8) * p[5] > 512)) return false;
    if ((p[1] & 2) && (!p[6] || p[6] > 64 || !p[7] || p[7] > 32 || ((p[6] + 7) / 8) * p[7] > 256)) return false;
    for (uint8_t i = 0; i < NRF5_UI_WIDGETS; i++) {
        const uint8_t *w = p + 16 + i * 4;
        if (w[0] >= OLED_DISPLAY_WIDTH || w[1] >= OLED_DISPLAY_HEIGHT || w[2] < 1 || w[2] > 2 || w[3] > 1) return false;
    }
    for (uint8_t i = 0; i < NRF5_UI_ENCODERS * NRF5_UI_LAYERS; i++) {
        uint8_t action = p[48 + i * 5];
        if (action > 3 && action != 255) return false;
#ifndef RGBLIGHT_ENABLE
        if (action == 1) return false;
#endif
#ifndef RGB_MATRIX_ENABLE
        if (action == 2) return false;
#endif
    }
    return crc32(p, NRF5_UI_CRC) == le32(p + NRF5_UI_CRC);
}
const uint8_t *nrf5_ui_config(void) {
    if (!loaded) {
        eeprom_read_block(active, (const void *)(uintptr_t)NRF5_UI_EEPROM_ADDR, NRF5_UI_SIZE);
        if (!valid(active)) defaults(active);
        loaded = true;
        nrf5_oled_set_power_mode(active[2]);
    }
    return active;
}
static void persist(void) {
    seal(active);
    eeprom_update_block(active, (void *)(uintptr_t)NRF5_UI_EEPROM_ADDR, NRF5_UI_SIZE);
    nrf5_oled_set_power_mode(active[2]);
}
bool nrf5_ui_encoder(uint8_t index, uint8_t layer, uint8_t *action, uint16_t *cw, uint16_t *ccw) {
    if (index >= NUM_ENCODERS || layer >= DYNAMIC_KEYMAP_LAYER_COUNT) return false;
    const uint8_t *p = nrf5_ui_config() + 48 + (layer * NRF5_UI_ENCODERS + index) * 5;
    if (*p == 255) return false;
    *action = p[0]; *cw = le16(p + 1); *ccw = le16(p + 3); return true;
}
// Default actions also back standard VIA encoder reads. Extended RGB actions
// are edited through the custom channel so their modifier behavior is retained.
#ifndef NRF5_ENCODER_BINDINGS
#    define NRF5_ENCODER_BINDINGS {{0,0,NRF5_ENCODER_KEYS,KC_VOLU,KC_VOLD}}
#endif
static const nrf5_encoder_binding_t bindings[] = NRF5_ENCODER_BINDINGS;
uint16_t nrf5_ui_encoder_key(uint8_t index, uint8_t layer, bool cw) {
    uint8_t action; uint16_t right, left;
    if (nrf5_ui_encoder(index,layer,&action,&right,&left)) return action == NRF5_ENCODER_KEYS ? (cw ? right : left) : KC_NO;
    for (uint8_t i = 0; i < ARRAY_SIZE(bindings); i++) if (bindings[i].index == index && bindings[i].layer == layer)
        return bindings[i].action == NRF5_ENCODER_KEYS ? (cw ? bindings[i].clockwise : bindings[i].counterclockwise) : KC_NO;
    return KC_NO;
}
void nrf5_ui_disconnect(uint8_t source) { if (session && owner == source) session = 0; }
bool nrf5_ui_command(uint8_t *d, uint8_t source) {
    if (d[0] == 0x14 || d[0] == 0x15) {
        if (d[1] >= DYNAMIC_KEYMAP_LAYER_COUNT || d[2] >= NUM_ENCODERS || d[3] > 1) { d[0] = 255; return true; }
        bool cw = d[3]; uint16_t key = nrf5_ui_encoder_key(d[2],d[1],cw);
        if (d[0] == 0x14) { d[4] = key >> 8; d[5] = key; }
        else {
            uint16_t other = nrf5_ui_encoder_key(d[2],d[1],!cw);
            uint8_t *p = active + 48 + (d[1] * NRF5_UI_ENCODERS + d[2]) * 5;
            p[0] = NRF5_ENCODER_KEYS;
            uint16_t right = cw ? ((uint16_t)d[4] << 8 | d[5]) : other;
            uint16_t left = cw ? other : ((uint16_t)d[4] << 8 | d[5]);
            p[1] = right; p[2] = right >> 8; p[3] = left; p[4] = left >> 8;
            session = 0; persist();
        }
        return true;
    }
    if ((d[0] != 7 && d[0] != 8 && d[0] != 9) || d[1] != 0x71) return false;
    nrf5_ui_config();
    if (session && timer_elapsed32(session_activity) > 30000) session = 0;
    uint8_t op = d[2], status = 0;
    uint16_t id = le16(d + 4), offset = le16(d + 6); uint8_t count = d[8];
    if (op == 0 && d[0] == 8) {
        memset(d + 4,0,28); d[4] = 1; d[5] = OLED_DISPLAY_WIDTH; d[6] = OLED_DISPLAY_HEIGHT;
        d[7] = NUM_ENCODERS; d[8] = DYNAMIC_KEYMAP_LAYER_COUNT; d[9] = (uint8_t)NRF5_UI_SIZE; d[10] = NRF5_UI_SIZE >> 8;
#ifdef RGBLIGHT_ENABLE
        d[11] |= 1;
#endif
#ifdef RGB_MATRIX_ENABLE
        d[11] |= 2;
#endif
    } else if (op == 1 && d[0] == 7) {
        if (d[4] > 1) status = 2;
        else if (session && owner != source) status = 3;
        else {
            if (++next_session == 0) ++next_session;
            session = next_session; owner = source; session_activity = timer_read32();
            if (d[4]) defaults(draft); else memcpy(draft,active,sizeof(draft));
            d[4] = session; d[5] = session >> 8;
        }
    } else if ((op == 2 && d[0] == 7) || (op == 3 && d[0] == 8)) {
        if (count > 23 || !count || offset > NRF5_UI_SIZE || count > NRF5_UI_SIZE - offset) status = 2;
        else if (id && (id != session || source != owner)) status = 3;
        else if (op == 2 && !id) status = 3;
        else if (op == 2) memcpy(draft + offset,d + 9,count);
        else memcpy(d + 9,(id ? draft : active) + offset,count);
    } else if ((op == 4 && d[0] == 9) || (op == 5 && d[0] == 7)) {
        if (!session || id != session || owner != source) status = 3;
        else if (op == 5) session = 0;
        else if (!valid(draft)) status = 4;
        else { memcpy(active,draft,sizeof(active)); session = 0; persist(); }
    } else status = 1;
    if (!status && session && owner == source) session_activity = timer_read32();
    d[3] = status; return true;
}
