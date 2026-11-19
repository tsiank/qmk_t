// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
RGB_MATRIX_EFFECT(MUSIC_SPECTRUM)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#    include "music.h"

static bool MUSIC_SPECTRUM(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    ciank67_music_render_spectrum(led_min, led_max, params->init);
    return rgb_matrix_check_finished_leds(led_max);
}
#endif
