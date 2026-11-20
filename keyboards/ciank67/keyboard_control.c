// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "music_stream.h"
#include "midi_typing.h"
#include "process_music.h"
#include "process_clicky.h"
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
    return ciank67_midi_typing_enabled() ? 3 : is_midi_on() ? 2 : is_music_on() ? 1 : 0;
}

static void set_mode(uint8_t mode) {
    // Physical QMK controls can enable both local music and MIDI. A summary
    // mode alone cannot establish that the requested exclusive state is set.
    if (ciank67_midi_typing_enabled() == (mode == 3) &&
        is_midi_on() == (mode == 2) && is_music_on() == (mode == 1)) return;
    ciank67_midi_typing_set(false);
    if (is_midi_on()) midi_off();
    if (is_music_on()) music_off();
    stop_all_notes();
    if (mode == 1) music_on();
    if (mode == 2) midi_on();
    if (mode == 3) ciank67_midi_typing_set(true);
}

void ciank67_keyboard_control_task(void) {
    uint8_t command[20];
    music_source_t source;
    bool apply;
    CRITICAL_REGION_ENTER();
    apply = ready;
    if (apply) { memcpy(command, pending, 20); source = pending_source; ready = false; }
    CRITICAL_REGION_EXIT();
    if (apply) {
        unsigned mask = command[3], value = command[4];
        if (mask & 1 && rgb_matrix_is_enabled() != !!(value & 1)) {
            (value & 1) ? rgb_matrix_enable() : rgb_matrix_disable();
        }
        if (mask & 2 && rgblight_is_enabled() != !!(value & 2)) {
            (value & 2) ? rgblight_enable() : rgblight_disable();
        }
        if (mask & 4 && is_audio_on() != !!(value & 4)) {
            (value & 4) ? audio_on() : audio_off();
        }
        if (mask & 8 && is_clicky_on() != !!(value & 8)) {
            (value & 8) ? clicky_on() : clicky_off();
        }
        if (mask & 16) set_mode(command[5]);
        memset(response, 0, sizeof(response));
        response[0] = 0xc7; response[1] = 0x83; response[2] = command[2];
        response[3] = rgb_matrix_is_enabled() | (rgblight_is_enabled() << 1) |
            (is_audio_on() << 2) | (is_clicky_on() << 3);
        response[4] = current_mode(); response[5] = 1; // protocol version
        response_source = source; reply_pending = true;
    }
    if (reply_pending) {
        bool sent = response_source == MUSIC_SOURCE_USB ? keyboard_control_usb_reply(response) : keyboard_control_ble_reply(response);
        if (sent) reply_pending = false;
    }
}
