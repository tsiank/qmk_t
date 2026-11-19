// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "lighting.h"
#include "music_stream.h"
#include "music.h"
#include "rgblight_drivers.h"

static bool underglow_owned;
static uint16_t last_frame;

static uint8_t levels[MUSIC_BANDS];
static uint8_t peaks[MUSIC_BANDS];
static uint32_t peak_at[MUSIC_BANDS];
static uint32_t spectrum_at;

uint8_t rgb_matrix_override_effect(uint8_t effect) {
    music_frame_t frame;
    // The core only permits overrides while enabled and not suspended. Keep
    // this guard as well so NONE can never be turned into a visible effect.
    if (effect && music_stream_read(&frame) && (frame.flags & MUSIC_MATRIX))
        return RGB_MATRIX_CUSTOM_MUSIC_SPECTRUM;
    return effect;
}

void ciank67_music_render_spectrum(uint8_t first, uint8_t last, bool init) {
    if (!rgb_matrix_is_enabled()) return;
    if (first == 0) {
        music_frame_t frame;
        bool active = music_stream_read(&frame) && (frame.flags & MUSIC_MATRIX);
        uint32_t now = timer_read32();
        uint32_t elapsed = init ? 0 : MIN(timer_elapsed32(spectrum_at), 100);
        spectrum_at = now;
        uint16_t speed = rgb_matrix_get_speed();
        uint16_t fall = ((400 + speed * 3) * elapsed + 999) / 1000;
        uint16_t peak_fall = ((250 + speed * 2) * elapsed + 999) / 1000;
        for (uint8_t band = 0; band < MUSIC_BANDS; ++band) {
            uint8_t target = active ? frame.bands[band] : 0;
            if (init || target >= levels[band]) levels[band] = target;
            else levels[band] = MAX(target, levels[band] > fall ? levels[band] - fall : 0);
            if (init || levels[band] >= peaks[band]) {
                peaks[band] = levels[band];
                peak_at[band] = now;
            } else if (timer_elapsed32(peak_at[band]) >= 250) {
                peaks[band] = MAX(levels[band], peaks[band] > peak_fall ? peaks[band] - peak_fall : 0);
            }
        }
    }
    uint8_t hue = rgb_matrix_get_hue(), sat = rgb_matrix_get_sat(), val = rgb_matrix_get_val();
    uint8_t flags = rgb_matrix_get_flags();
    last = MIN(last, RGB_MATRIX_LED_COUNT);
    for (uint8_t i = first; i < last; ++i) {
        if (!(g_led_config.flags[i] & flags)) continue;
        uint8_t band = MIN((uint16_t)g_led_config.point[i].x * MUSIC_BANDS / 225, MUSIC_BANDS - 1);
        // Five physical key rows, with fractional brightness at the bar edge.
        uint8_t row = MIN((64 - MIN(g_led_config.point[i].y, 64) + 8) / 16, 4);
        uint16_t base = row * 51;
        uint8_t fill = levels[band] > base ? MIN(levels[band] - base, 51) : 0;
        bool peak = peaks[band] && row == (peaks[band] - 1) / 51;
        hsv_t hsv = {(uint8_t)(hue + band * 12), peak ? sat / 4 : sat,
                     peak ? val : (uint16_t)val * fill / 51};
        rgb_t color = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, color.r, color.g, color.b);
    }
}

void ciank67_music_task(void) {
    if (ciank67_underglow_test_is_active()) return;
    music_frame_t frame;
    bool active = music_stream_read(&frame) && (frame.flags & MUSIC_UNDERGLOW);
    if (!active) {
        if (underglow_owned) {
            underglow_owned = false;
            if (rgblight_is_enabled()) rgblight_mode_noeeprom(rgblight_get_mode());
            else rgblight_set();
        }
        return;
    }
    // A manual off remains off; rgblight_set still renders the Caps indicator.
    if (!rgblight_is_enabled()) {
        if (underglow_owned) {
            underglow_owned = false;
            rgblight_set();
        }
        return;
    }
    rgblight_timer_disable();
    if (underglow_owned && timer_elapsed(last_frame) < 33) return;
    underglow_owned = true;
    last_frame = timer_read();
    for (uint8_t i = 0; i < RGBLIGHT_LED_COUNT; ++i) {
        uint8_t band = (uint16_t)i * MUSIC_BANDS / RGBLIGHT_LED_COUNT;
        uint8_t value = (uint16_t)frame.bands[band] * rgblight_get_val() / 255;
        rgb_t color = hsv_to_rgb((hsv_t){band * 12, 255, value});
        rgblight_driver.set_color(i, color.r, color.g, color.b);
    }
    // Apply Caps Lock after the music frame, using the existing layer/power path.
    rgblight_set();
}
