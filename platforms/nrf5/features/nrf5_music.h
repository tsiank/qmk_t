// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

void nrf5_music_render_spectrum(uint8_t first, uint8_t last, bool init);
void nrf5_music_render_dual_dance(uint8_t first, uint8_t last, bool init);
