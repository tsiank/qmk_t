/*
Copyright 2018 Sekigon

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ws2812.h"
#include "nrfx_pwm.h"

// A common-mode sample is one whole PWM period, not one sample per channel.
#define NS_TO_TICKS(ns) (((ns) * 16UL + 500) / 1000)
#define PWM_PERIOD NS_TO_TICKS(WS2812_TIMING)
#define PWM_ZERO NS_TO_TICKS(WS2812_T0H)
#define PWM_ONE NS_TO_TICKS(WS2812_T1H)
#define RESET_SAMPLES ((WS2812_TRST_US * 1000UL + WS2812_TIMING - 1) / WS2812_TIMING)
#ifndef NRF_WS2812_TRAILING_LEDS
#    define NRF_WS2812_TRAILING_LEDS 64
#endif
#define TRAILING_SAMPLES (NRF_WS2812_TRAILING_LEDS * 24)
#define MAX_FRAME_SAMPLES (WS2812_LED_COUNT * 32 + TRAILING_SAMPLES + RESET_SAMPLES)

_Static_assert(PWM_ZERO > 0 && PWM_ONE < PWM_PERIOD, "Invalid WS2812 timing");
_Static_assert(MAX_FRAME_SAMPLES <= 0x7FFF, "WS2812 frame exceeds nRF PWM EasyDMA capacity");

static const nrfx_pwm_t pwm = NRFX_PWM_INSTANCE(0);
static bool initialized;
static ws2812_led_t pixels[WS2812_LED_COUNT];
// EasyDMA reads RAM. Keep the full frame alive and avoid a large IRQ stack frame.
static nrf_pwm_values_common_t frame[MAX_FRAME_SAMPLES];

static bool init_pwm(uint8_t pin) {
    if (initialized) {
        return true;
    }
    nrfx_pwm_config_t config = NRFX_PWM_DEFAULT_CONFIG;
    config.output_pins[0] = pin;
    config.output_pins[1] = NRFX_PWM_PIN_NOT_USED;
    config.output_pins[2] = NRFX_PWM_PIN_NOT_USED;
    config.output_pins[3] = NRFX_PWM_PIN_NOT_USED;
    config.base_clock = NRF_PWM_CLK_16MHz;
    config.count_mode = NRF_PWM_MODE_UP;
    config.top_value = PWM_PERIOD;
    config.load_mode = NRF_PWM_LOAD_COMMON;
    config.step_mode = NRF_PWM_STEP_AUTO;
    // Completion is polled in the main loop; no PWM callback is required.
    initialized = nrfx_pwm_init(&pwm, &config, NULL) == NRFX_SUCCESS;
    return initialized;
}

static uint16_t encode_byte(uint16_t offset, uint8_t value) {
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        frame[offset++] = 0x8000 | ((value & mask) ? PWM_ONE : PWM_ZERO);
    }
    return offset;
}

static uint16_t encode_pixel(uint16_t offset, uint8_t red, uint8_t green, uint8_t blue) {
#if WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_RGB
    offset = encode_byte(offset, red);
    offset = encode_byte(offset, green);
    return encode_byte(offset, blue);
#elif WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_BGR
    offset = encode_byte(offset, blue);
    offset = encode_byte(offset, green);
    return encode_byte(offset, red);
#else
    offset = encode_byte(offset, green);
    offset = encode_byte(offset, red);
    return encode_byte(offset, blue);
#endif
}

static void send_frame(uint16_t length) {
    // Preserve stable's trailing 64 black RGB pixels. Their electrical purpose
    // is board-specific; they are additional to the actual reset-low interval.
    for (uint16_t i = 0; i < TRAILING_SAMPLES; ++i) {
        frame[length++] = 0x8000 | PWM_ZERO;
    }
    // Send actual low levels for the entire latch interval. Appending black
    // pixels still produces high pulses and is not a WS2812 reset signal.
    for (uint16_t i = 0; i < RESET_SAMPLES; ++i) {
        frame[length++] = 0x8000;
    }
    nrf_pwm_sequence_t sequence = {
        .values = {.p_common = frame},
        .length = length,
        .repeats = 0,
        .end_delay = 0,
    };
    nrfx_pwm_simple_playback(&pwm, &sequence, 1, NRFX_PWM_FLAG_STOP);
    while (!nrfx_pwm_is_stopped(&pwm)) {}
}

void ws2812_init(void) {
    init_pwm(WS2812_DI_PIN);
}

void ws2812_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index >= 0 && index < WS2812_LED_COUNT) {
        pixels[index].r = red;
        pixels[index].g = green;
        pixels[index].b = blue;
#ifdef WS2812_RGBW
        pixels[index].w = 0;
#endif
    }
}

void ws2812_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < WS2812_LED_COUNT; ++i) {
        ws2812_set_color(i, red, green, blue);
    }
}

void ws2812_flush(void) {
    if (!init_pwm(WS2812_DI_PIN)) {
        return;
    }
    uint16_t offset = 0;
    for (uint16_t i = 0; i < WS2812_LED_COUNT; ++i) {
        ws2812_led_t pixel = pixels[i];
#ifdef WS2812_RGBW
        ws2812_rgb_to_rgbw(&pixel);
#endif
        offset = encode_pixel(offset, pixel.r, pixel.g, pixel.b);
#ifdef WS2812_RGBW
        offset = encode_byte(offset, pixel.w);
#endif
    }
    send_frame(offset);
}

// Legacy nRF5 callers use RGB structs rather than QMK's WS2812 pixel type.
void ws2812_setleds_pin(rgb_led_t *leds, uint16_t count, uint8_t pin) {
    if (count > WS2812_LED_COUNT || !init_pwm(pin)) {
        return;
    }
    uint16_t offset = 0;
    for (uint16_t i = 0; i < count; ++i) {
        offset = encode_pixel(offset, leds[i].r, leds[i].g, leds[i].b);
    }
    send_frame(offset);
}

void ws2812_setleds(rgb_led_t *leds, uint16_t count) {
    ws2812_setleds_pin(leds, count, WS2812_DI_PIN);
}

void ws2812_setleds_rgbw(rgb_led_t *leds, uint16_t count) {
    if (count > WS2812_LED_COUNT || !init_pwm(WS2812_DI_PIN)) {
        return;
    }
    uint16_t offset = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t white = MIN(leds[i].r, MIN(leds[i].g, leds[i].b));
        offset = encode_pixel(offset, leds[i].r - white, leds[i].g - white, leds[i].b - white);
        offset = encode_byte(offset, white);
    }
    send_frame(offset);
}
