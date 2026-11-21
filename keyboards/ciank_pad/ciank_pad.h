// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quantum.h"

enum ciank_pad_layers { _BASE, _DIR, _RGB_SETTINGS, _SYS, _BLE_SETTINGS };
// Preserve the original 15 USER codes for existing custom keymaps.
enum ciank_pad_keycodes { DISC = SAFE_RANGE, ADVW, ADVS, SEL0, SEL1, SEL2, DELB, SLEEP, REBOOT, ENT_DFU, RGBRST, RGBG_TOG, BATT_LV, BATT_LV2, VOLUP, LIGHT_STATUS, UNDERGLOW_TEST, MIDI_TYPING_TOGGLE };
