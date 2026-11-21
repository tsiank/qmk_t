// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
RGB_MATRIX_EFFECT(MUSIC_SPECTRUM)
RGB_MATRIX_EFFECT(MUSIC_DUAL_DANCE)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#    include "nrf5_music.h"

static bool MUSIC_SPECTRUM(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    nrf5_music_render_spectrum(led_min, led_max, params->init);
    return rgb_matrix_check_finished_leds(led_max);
}

static bool MUSIC_DUAL_DANCE(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    nrf5_music_render_dual_dance(led_min, led_max, params->init);
    return rgb_matrix_check_finished_leds(led_max);
}
#endif
