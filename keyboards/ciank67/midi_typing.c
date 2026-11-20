// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "midi_typing.h"
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
#    include "process_midi.h"
#    include "process_music.h"
#    include <string.h>

static bool enabled;
// Store the note on press: a layer change must not change its Note Off.
static uint8_t notes[MATRIX_ROWS][MATRIX_COLS];
static uint8_t references[128];

// Transport disconnect/reset runs this in thread mode after clearing queues.
void nrf_midi_reset_user(void) {
    memset(notes, 0, sizeof(notes));
    memset(references, 0, sizeof(references));
}

static void release_all(void) {
    for (unsigned note = 0; note < 128; note++) {
        if (references[note]) process_midi_basic_noteoff(note);
    }
    process_midi_all_notes_off();
    memset(notes, 0, sizeof(notes));
    memset(references, 0, sizeof(references));
}
#endif

bool ciank67_process_midi_typing(uint16_t keycode, keyrecord_t *record, uint16_t toggle) {
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
    if (keycode == toggle) {
        if (record->event.pressed) {
            release_all();
            enabled = !enabled;
            if (enabled) {
                if (is_midi_on()) midi_off();
                if (is_music_on()) music_off();
            }
        }
        return false;
    }
    // Pure performance modes and MI_OFF also exit typing accompaniment.
    if (record->event.pressed && (keycode == QK_MIDI_ON || keycode == QK_MIDI_TOGGLE ||
        keycode == QK_MIDI_OFF || keycode == QK_MUSIC_ON || keycode == QK_MUSIC_TOGGLE)) {
        if (enabled) release_all();
        enabled = false;
    }
    if (!enabled) return true;
    unsigned row = record->event.key.row, col = record->event.key.col;
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return true;
    uint8_t *held = &notes[row][col];
    if (!record->event.pressed) {
        if (*held) {
            if (--references[*held] == 0) process_midi_basic_noteoff(*held);
            *held = 0;
        }
        return true;
    }
    // Tap-hold keys sound only when QMK resolves them as a tap. Modifiers,
    // layer changes, lighting controls and mouse keys retain their own action.
    if (IS_QK_MOD_TAP(keycode) || IS_QK_LAYER_TAP(keycode)) {
        if (!record->tap.count) return true;
        keycode &= 0xff;
    } else if (IS_QK_MODS(keycode)) {
        keycode &= 0xff;
    }
    if (keycode < KC_A || keycode > KC_SLSH || *held) return true;
    uint8_t note = 36 + 12 * (MATRIX_ROWS - row - 1) + col;
    *held = note;
    if (references[note]++ == 0) process_midi_basic_noteon(note);
#endif
    return true;
}

bool ciank67_midi_typing_enabled(void) {
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
    return enabled;
#else
    return false;
#endif
}

void ciank67_midi_typing_set(bool on) {
#if defined(MIDI_ENABLE) && defined(MIDI_BASIC)
    if (enabled == on) return;
    release_all();
    enabled = on;
    if (on) {
        if (is_midi_on()) midi_off();
        if (is_music_on()) music_off();
    }
#endif
}
