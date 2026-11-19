// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "quantum.h"

bool ciank67_process_lighting(uint16_t keycode, keyrecord_t *record, uint16_t combined_toggle);
void ciank67_lighting_report(void);
void ciank67_lighting_task(void);
void ciank67_underglow_test(void);
void ciank67_underglow_test_task(void);
