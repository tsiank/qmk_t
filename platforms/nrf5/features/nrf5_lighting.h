// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "quantum.h"
#include "../lighting_runtime.h"

bool nrf5_process_lighting(uint16_t keycode, keyrecord_t *record, uint16_t combined_toggle);
void nrf5_lighting_report(void);
void nrf5_lighting_task(void);
void nrf5_underglow_test(void);
void nrf5_underglow_test_task(void);
bool nrf5_underglow_test_is_active(void);
#ifdef MUSIC_STREAM_ENABLE
void nrf5_music_task(void);
#endif
