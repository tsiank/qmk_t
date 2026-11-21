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

static uint8_t pending[20];
static music_source_t pending_source;
static bool ready;
static uint8_t response[20];
static music_source_t response_source;
static bool reply_pending;
extern bool keyboard_control_usb_reply(const uint8_t *data);
extern bool keyboard_control_ble_reply(const uint8_t *data);

bool keyboard_control_receive(const uint8_t *data, music_source_t source) {
    if (data[1] != 3 && data[1] != 4) return false;
    if (data[3] & ~31 || data[4] & ~15 || data[5] > 3) return false;
    if (data[1] == 4 && (data[3] || data[4] || data[5])) return false;
    for (unsigned i = 6; i < 20; i++) if (data[i]) return false;
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
        unsigned mask = command[3], value = command[4];
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
        response[4] = current_mode(); response[5] = 1; // protocol version
        response_source = source; reply_pending = true;
    }
    if (reply_pending) {
        bool sent = response_source == MUSIC_SOURCE_USB ? keyboard_control_usb_reply(response) : keyboard_control_ble_reply(response);
        if (sent) reply_pending = false;
    }
}
