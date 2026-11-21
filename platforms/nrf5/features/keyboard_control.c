// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "music_stream.h"
#include "nrf5_midi_typing.h"
#if defined(AUDIO_ENABLE) || (defined(MIDI_ENABLE) && defined(MIDI_BASIC))
#include "process_music.h"
#endif
#if defined(AUDIO_ENABLE) && defined(AUDIO_CLICKY)
#include "process_clicky.h"
#endif
#include "app_util_platform.h"
#include <string.h>
#ifdef NRF5_CLOCK_ENABLE
#    include "nrf5_clock.h"
#endif

#ifdef NRF5_OLED_STATUS_ENABLE
#    include "nrf5_ui.h"
#    include "oled_driver.h"
#endif

static uint8_t pending[20];
static music_source_t pending_source;
static bool ready;
static uint8_t response[20];
static music_source_t response_source;
static bool reply_pending;
extern bool keyboard_control_usb_reply(const uint8_t *data);
extern bool keyboard_control_ble_reply(const uint8_t *data);

bool keyboard_control_receive(const uint8_t *data, music_source_t source) {
    if (data[1] == 5) {
#ifdef NRF5_CLOCK_ENABLE
        if (data[3] >= 24 || data[4] >= 60 || data[5] >= 60 || ((uint16_t)data[6] | ((uint16_t)data[7] << 8)) >= 1000) return false;
        for (unsigned i = 8; i < 20; i++) if (data[i]) return false;
#else
        return false;
#endif
    } else if (data[1] == 6) {
#ifdef NRF5_OLED_STATUS_ENABLE
        if (data[3] > 2) return false;
        for (unsigned i = 4; i < 20; i++) if (data[i]) return false;
#else
        return false;
#endif
    } else {
        if (data[1] != 3 && data[1] != 4) return false;
        if (data[3] & ~31 || data[4] & ~15 || data[5] > 3) return false;
        if (data[1] == 4 && (data[3] || data[4] || data[5])) return false;
        for (unsigned i = 6; i < 20; i++) if (data[i]) return false;
    }
    CRITICAL_REGION_ENTER();
    memcpy(pending, data, 20);
    pending_source = source;
    ready = true;
    CRITICAL_REGION_EXIT();
    return true;
}

static uint8_t current_mode(void) {
    if (nrf5_midi_typing_enabled()) return 3;
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
    if (is_midi_on()) return 2;
#endif
#ifdef AUDIO_ENABLE
    if (is_music_on()) return 1;
#endif
    return 0;
}

static void set_mode(uint8_t mode) {
    bool midi = false, music = false;
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
    midi = is_midi_on();
#endif
#if defined(AUDIO_ENABLE) || (defined(MIDI_ENABLE) && defined(MIDI_BASIC))
    music = is_music_on();
#endif
    if (nrf5_midi_typing_enabled() == (mode == 3) && midi == (mode == 2) && music == (mode == 1)) return;
    nrf5_midi_typing_set(false);
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
    if (midi) midi_off();
#endif
#if defined(AUDIO_ENABLE) || (defined(MIDI_ENABLE) && defined(MIDI_BASIC))
    if (music) music_off();
#endif
#ifdef AUDIO_ENABLE
    stop_all_notes();
    if (mode == 1) music_on();
#endif
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
    if (mode == 2) midi_on();
    if (mode == 3) nrf5_midi_typing_set(true);
#endif
}

void nrf5_keyboard_control_task(void) {
    uint8_t command[20];
    music_source_t source;
    bool apply;
    CRITICAL_REGION_ENTER();
    apply = ready;
    if (apply) { memcpy(command, pending, 20); source = pending_source; ready = false; }
    CRITICAL_REGION_EXIT();
    if (apply) {
#ifdef NRF5_CLOCK_ENABLE
        if (command[1] == 5) nrf5_clock_set(command[3], command[4], command[5], (uint16_t)command[6] | ((uint16_t)command[7] << 8));
#endif
#ifdef NRF5_OLED_STATUS_ENABLE
        if (command[1] == 6) nrf5_oled_set_power_mode(command[3]);
#endif
        unsigned mask = command[1] == 3 ? command[3] : 0, value = command[4];
        (void)value; // Keyboards without RGB/audio still support mode control.
#ifdef RGB_MATRIX_ENABLE
        if (mask & 1 && rgb_matrix_is_enabled() != !!(value & 1)) {
            (value & 1) ? rgb_matrix_enable() : rgb_matrix_disable();
        }
#endif
#ifdef RGBLIGHT_ENABLE
        if (mask & 2 && rgblight_is_enabled() != !!(value & 2)) {
            (value & 2) ? rgblight_enable() : rgblight_disable();
        }
#endif
#ifdef AUDIO_ENABLE
        if (mask & 4 && is_audio_on() != !!(value & 4)) {
            (value & 4) ? audio_on() : audio_off();
        }
#endif
#if defined(AUDIO_ENABLE) && defined(AUDIO_CLICKY)
        if (mask & 8 && is_clicky_on() != !!(value & 8)) {
            (value & 8) ? clicky_on() : clicky_off();
        }
#endif
        if (mask & 16) set_mode(command[5]);
        memset(response, 0, sizeof(response));
        response[0] = 0xc7; response[1] = 0x83; response[2] = command[2];
        response[3] = 0;
#ifdef RGB_MATRIX_ENABLE
        response[3] |= rgb_matrix_is_enabled();
#endif
#ifdef RGBLIGHT_ENABLE
        response[3] |= rgblight_is_enabled() << 1;
#endif
#ifdef AUDIO_ENABLE
        response[3] |= is_audio_on() << 2;
#endif
#if defined(AUDIO_ENABLE) && defined(AUDIO_CLICKY)
        response[3] |= is_clicky_on() << 3;
#endif
        response[4] = current_mode(); response[5] = 1; // legacy state
#if defined(KEYBOARD_CONTROL_CAPABILITIES) || defined(NRF5_CLOCK_ENABLE) || defined(NRF5_OLED_STATUS_ENABLE)
        response[5] = 2; // state + hardware capabilities
#    ifdef RGB_MATRIX_ENABLE
        response[6] |= 1;
#    endif
#    ifdef RGBLIGHT_ENABLE
        response[6] |= 2;
#    endif
#    ifdef AUDIO_ENABLE
        response[6] |= 4 | 16;
        response[7] |= 1; // local music
#    endif
#    if defined(AUDIO_ENABLE) && defined(AUDIO_CLICKY)
        response[6] |= 8;
#    endif
#    if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
        response[6] |= 16;
        response[7] |= 2 | 4; // MIDI and typing accompaniment
#    endif
#endif
#ifdef NRF5_CLOCK_ENABLE
        response[5] = 3; // v2 hardware fields + clock capability/status
        response[8] = 1;
        uint8_t hour, minute, second;
        response[9] = nrf5_clock_read(&hour, &minute, &second);
#endif
#ifdef NRF5_OLED_STATUS_ENABLE
        response[5] = 3;
        response[8] |= 2; // independently optional screen power control
        response[10] = nrf5_oled_get_power_mode();
        response[11] = is_oled_on();
#endif
        response_source = source; reply_pending = true;
    }
    if (reply_pending) {
        bool sent = response_source == MUSIC_SOURCE_USB ? keyboard_control_usb_reply(response) : keyboard_control_ble_reply(response);
        if (sent) reply_pending = false;
    }
}
