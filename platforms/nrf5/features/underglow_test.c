// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_lighting.h"
#if defined(RGBLIGHT_ENABLE) && defined(RGBLIGHT_LAYERS) && defined(RGBLIGHT_WS2812)
#include "nrf5/ws2812.h"
#include <string.h>

#ifndef NRF5_UNDERGLOW_TEST_SPLIT
#    define NRF5_UNDERGLOW_TEST_SPLIT (RGBLIGHT_LED_COUNT / 2)
#endif
#ifndef NRF5_UNDERGLOW_TEST_WHITE_COUNT
#    define NRF5_UNDERGLOW_TEST_WHITE_COUNT (RGBLIGHT_LED_COUNT - NRF5_UNDERGLOW_TEST_SPLIT)
#endif
static bool active;
static uint32_t started;
static uint16_t last_frame;
static const rgblight_segment_t *const *saved_layers;
static rgb_led_t test_pixels[RGBLIGHT_LED_COUNT];

bool nrf5_underglow_test_is_active(void) { return active; }

static void finish_test(void) {
    active = false;
    rgblight_layers = saved_layers;
    if (rgblight_is_enabled()) {
        rgblight_mode_noeeprom(rgblight_get_mode());
    } else {
        rgblight_set();
    }
}

void nrf5_underglow_test(void) {
    if (active) {
        finish_test();
        return;
    }
    saved_layers = rgblight_layers;
    rgblight_layers = NULL;
    rgblight_timer_disable();
#if defined(RGB_PWR_PIN) || defined(RGB_PWR_PIN_REVERSE)
    rgb_pwr_on();
#endif
    started = timer_read32();
    last_frame = timer_read() - 20;
    active = true;
}

void nrf5_underglow_test_task(void) {
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
        if (phase == 1 && i < NRF5_UNDERGLOW_TEST_SPLIT) test_pixels[i].g = 64;
        if (phase == 2 && i >= NRF5_UNDERGLOW_TEST_SPLIT) test_pixels[i].b = 64;
        if (phase == 3 && i >= NRF5_UNDERGLOW_TEST_SPLIT && i < NRF5_UNDERGLOW_TEST_SPLIT + NRF5_UNDERGLOW_TEST_WHITE_COUNT)
            test_pixels[i].r = test_pixels[i].g = test_pixels[i].b = 64;
    }
    ws2812_setleds(test_pixels, RGBLIGHT_LED_COUNT);
}

#else
bool nrf5_underglow_test_is_active(void) { return false; }
void nrf5_underglow_test(void) {}
void nrf5_underglow_test_task(void) {}
#endif
