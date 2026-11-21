// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quantum.h"
bool nrf5_process_midi_typing(uint16_t keycode, keyrecord_t *record, uint16_t toggle);
bool nrf5_midi_typing_enabled(void);
void nrf5_midi_typing_set(bool on);
