// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lighting.h"
#include "nrf5/ws2812.h"
#include <string.h>

static bool active;
static uint32_t started;
static uint16_t last_frame;
static const rgblight_segment_t *const *saved_layers;
static rgb_led_t test_pixels[RGBLIGHT_LED_COUNT];

static void finish_test(void) {
    active = false;
    rgblight_layers = saved_layers;
    if (rgblight_is_enabled()) {
        rgblight_mode_noeeprom(rgblight_get_mode());
    } else {
        memset(test_pixels, 0, sizeof(test_pixels));
        ws2812_setleds(test_pixels, RGBLIGHT_LED_COUNT);
        rgb_pwr_off();
    }
}

void ciank67_underglow_test(void) {
    if (active) {
        finish_test();
        return;
    }
    saved_layers = rgblight_layers;
    rgblight_layers = NULL;
    rgblight_timer_disable();
    rgb_pwr_on();
    started = timer_read32();
    last_frame = timer_read() - 20;
    active = true;
}

void ciank67_underglow_test_task(void) {
    if (!active) return;
    uint32_t phase = timer_elapsed32(started) / 2000;
    if (phase >= 4) {
        finish_test();
        return;
    }
    rgblight_timer_disable();
    if (timer_elapsed(last_frame) < 20) return;
    last_frame = timer_read();
    // Bypass rgblight effects, layers and the modern pixel buffer. Send a
    // complete frame through the same array interface used by stable.
    memset(test_pixels, 0, sizeof(test_pixels));
    for (unsigned i = 0; i < RGBLIGHT_LED_COUNT; ++i) {
        if (phase == 0) test_pixels[i].r = 64;
        if (phase == 1 && i < 9) test_pixels[i].g = 64;
        if (phase == 2 && i >= 9) test_pixels[i].b = 64;
        if (phase == 3 && i >= 9 && i < 17)
            test_pixels[i].r = test_pixels[i].g = test_pixels[i].b = 64;
    }
    ws2812_setleds(test_pixels, RGBLIGHT_LED_COUNT);
}
