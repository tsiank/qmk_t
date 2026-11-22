// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H

// Build-system placeholder only. The dongle entry point never initializes
// QMK or scans this matrix. Pairing uses the USB insertion window + radio;
// optional management and DFU use the independent vendor USB interface.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO),
};

