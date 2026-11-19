// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "lighting.h"
#include "music_stream.h"
#include "music.h"
#include "rgblight_drivers.h"

static bool underglow_owned;
static uint16_t last_frame;

// Two matrix columns per bar keep all five staggered key rows in the
// same frequency group. The host still sends sixteen bands unchanged.
#define SPECTRUM_BARS 7
static int32_t spectrum_levels[SPECTRUM_BARS]; // Q8 heights.
static uint8_t spectrum_bar[RGB_MATRIX_LED_COUNT];
static uint32_t spectrum_at;
static bool spectrum_monochrome;
static bool spectrum_custom_color;
static music_rgb_t spectrum_color;

// Incoming levels are compressed dB values. A linear height mapping fills
// several rows even at moderate sound levels. Remove the very quiet floor,
// then square the display level to give musical peaks more visible travel.
static uint16_t music_extent(uint8_t level) {
    if (level <= 32) return 0;
    uint16_t scaled = (uint16_t)(level - 32) * 255 / (255 - 32);
    return (uint32_t)scaled * scaled * 256 / 255;
}

static rgb_t selected_rgb(music_rgb_t color, uint8_t brightness) {
    return (rgb_t){(uint16_t)color.r * brightness / 255,
                   (uint16_t)color.g * brightness / 255,
                   (uint16_t)color.b * brightness / 255};
}

uint8_t rgb_matrix_override_effect(uint8_t effect) {
    music_frame_t frame;
    // The core only permits overrides while enabled and not suspended. Keep
    // this guard as well so NONE can never be turned into a visible effect.
    if (effect && music_stream_read(&frame) && (frame.flags & MUSIC_MATRIX))
        return frame.effect == MUSIC_EFFECT_DUAL_DANCE ? RGB_MATRIX_CUSTOM_MUSIC_DUAL_DANCE : RGB_MATRIX_CUSTOM_MUSIC_SPECTRUM;
    return effect;
}

void ciank67_music_render_spectrum(uint8_t first, uint8_t last, bool init) {
    if (!rgb_matrix_is_enabled()) return;
    if (first == 0) {
        if (init) {
            for (uint8_t bar = 0; bar < SPECTRUM_BARS; ++bar) spectrum_levels[bar] = 0;
            for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; ++i)
                spectrum_bar[i] = MIN((uint16_t)g_led_config.point[i].x * SPECTRUM_BARS / 225, SPECTRUM_BARS - 1);
            for (uint8_t row = 0; row < MATRIX_ROWS; ++row) {
                for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
                    uint8_t led = g_led_config.matrix_co[row][col];
                    if (led < RGB_MATRIX_LED_COUNT) spectrum_bar[led] = (uint16_t)col * SPECTRUM_BARS / MATRIX_COLS;
                }
            }
        }
        music_frame_t frame;
        bool active = music_stream_read(&frame) && (frame.flags & MUSIC_MATRIX);
        spectrum_monochrome = active && frame.monochrome;
        spectrum_custom_color = spectrum_monochrome && frame.custom_color;
        spectrum_color = frame.color;
        uint32_t elapsed = init ? 16 : MIN(timer_elapsed32(spectrum_at), 100);
        spectrum_at = timer_read32();
        // The host already smooths FFT bands. Follow attacks immediately and
        // only cap the fall rate here; a second low-pass filter erases beats.
        uint16_t release_ms = 120 - (uint16_t)rgb_matrix_get_speed() * 80 / 255;
        int32_t fall = (int32_t)255 * 256 * elapsed / release_ms;
        for (uint8_t bar = 0; bar < SPECTRUM_BARS; ++bar) {
            uint8_t level = 0;
            if (active) {
                for (uint8_t band = bar * MUSIC_BANDS / SPECTRUM_BARS;
                     band < (bar + 1) * MUSIC_BANDS / SPECTRUM_BARS; ++band)
                    level = MAX(level, frame.bands[band]);
            }
            int32_t target = (int32_t)level * 256;
            if (target >= spectrum_levels[bar]) spectrum_levels[bar] = target;
            else spectrum_levels[bar] = MAX(target, spectrum_levels[bar] - fall);
        }
    }
    uint8_t hue = rgb_matrix_get_hue(), sat = rgb_matrix_get_sat(), val = rgb_matrix_get_val();
    uint8_t flags = rgb_matrix_get_flags();
    last = MIN(last, RGB_MATRIX_LED_COUNT);
    for (uint8_t i = first; i < last; ++i) {
        if (!(g_led_config.flags[i] & flags)) continue;
        uint8_t row = MIN((64 - MIN(g_led_config.point[i].y, 64) + 8) / 16, 4);
        uint8_t level = spectrum_levels[spectrum_bar[i]] / 256;
        int32_t fill = music_extent(level) - (int32_t)row * 51 * 256;
        // Fractional row coverage fades the bar edge. No detached white peak
        // markers: every lit key belongs to a contiguous bottom-up column.
        uint8_t body_value = (uint16_t)val * level / 255;
        uint8_t brightness = fill > 0 ? MIN((int32_t)body_value * fill / (51 * 256), body_value) : 0;
        uint8_t color_hue = spectrum_monochrome ? hue : hue + (4 - row) * 85 / 4;
        rgb_t color = spectrum_custom_color ? selected_rgb(spectrum_color, brightness) : hsv_to_rgb((hsv_t){color_hue, sat, brightness});
        rgb_matrix_set_color(i, color.r, color.g, color.b);
    }
}

// Two filled, concentric color fields expand with the same audio envelope.
// Geometry is derived from this board's LED positions rather than another
// keyboard's preset key indices. All animation is continuous in time.
static uint32_t dance_at;
static uint16_t dance_phase;
static int32_t dance_envelope; // Q8, 0 .. 255*256.
static bool dance_monochrome;
static bool dance_custom_color;
static music_rgb_t dance_color;

static uint8_t dance_distance(int16_t x, int16_t y) {
    uint16_t squared = x * x + y * y;
    uint8_t root = 0;
    for (uint8_t bit = 64; bit; bit >>= 1) {
        uint8_t candidate = root | bit;
        if ((uint16_t)candidate * candidate <= squared) root = candidate;
    }
    return root;
}

void ciank67_music_render_dual_dance(uint8_t first, uint8_t last, bool init) {
    if (!rgb_matrix_is_enabled()) return;
    if (first == 0) {
        music_frame_t frame;
        bool active = music_stream_read(&frame) && (frame.flags & MUSIC_MATRIX);
        dance_monochrome = active && frame.monochrome;
        dance_custom_color = dance_monochrome && frame.custom_color;
        dance_color = frame.color;
        uint32_t elapsed = init ? 16 : MIN(timer_elapsed32(dance_at), 100);
        dance_at = timer_read32();
        if (init) { dance_envelope = 0; dance_phase = 0; }
        uint8_t peak = 0, second = 0;
        if (active) {
            for (uint8_t band = 0; band < MUSIC_BANDS; ++band) {
                uint8_t level = frame.bands[band];
                if (level >= peak) { second = peak; peak = level; }
                else second = MAX(second, level);
            }
        }
        // Follow dominant frequency energy without diluting a single musical
        // note across sixteen mostly empty bands. No periodic retriggers.
        int32_t target = ((peak * 3 + second) / 4) * 256;
        int32_t delta = target - dance_envelope;
        uint16_t smoothing = delta > 0 ? 10 : 45;
        if (elapsed && delta) {
            int32_t step = delta * (int32_t)elapsed / (int32_t)(smoothing + elapsed);
            dance_envelope += step ? step : (delta > 0 ? 1 : -1);
        }
        if (!target && dance_envelope < 256) dance_envelope = 0;
        // Phase is Q8 hue. Speed controls color flow, not beat detection.
        dance_phase += elapsed * (3 + rgb_matrix_get_speed() / 32);
    }
    uint8_t hue = rgb_matrix_get_hue(), sat = rgb_matrix_get_sat(), val = rgb_matrix_get_val();
    uint8_t flags = rgb_matrix_get_flags();
    uint8_t intensity = dance_envelope / 256;
    uint16_t radius = (uint32_t)music_extent(intensity) * 72 / 255;
    last = MIN(last, RGB_MATRIX_LED_COUNT);
    for (uint8_t i = first; i < last; ++i) {
        if (!(g_led_config.flags[i] & flags)) continue;
        int16_t x = (int16_t)g_led_config.point[i].x - (g_led_config.point[i].x < 112 ? 56 : 168);
        int16_t y = (int16_t)g_led_config.point[i].y - 32;
        uint8_t distance = dance_distance(x, y);
        uint16_t edge = radius > distance * 256 ? MIN((radius - distance * 256) / 16, 255) : 0;
        uint8_t ring = distance / 16;
        uint8_t color_hue = dance_monochrome ? hue : hue + (dance_phase >> 8) - ring * 51;
        uint8_t brightness = (uint16_t)val * edge / 255;
        brightness = (uint16_t)brightness * intensity / 255;
        rgb_t color = dance_custom_color ? selected_rgb(dance_color, brightness) : hsv_to_rgb((hsv_t){color_hue, sat, brightness});
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
        rgb_t color = hsv_to_rgb((hsv_t){frame.monochrome ? rgblight_get_hue() : band * 12,
                                       frame.monochrome ? rgblight_get_sat() : 255, value});
        if (frame.monochrome && frame.custom_color) color = selected_rgb(frame.color, value);
        rgblight_driver.set_color(i, color.r, color.g, color.b);
    }
    // Apply Caps Lock after the music frame, using the existing layer/power path.
    rgblight_set();
}
