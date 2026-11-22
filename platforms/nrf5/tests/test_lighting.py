# Copyright 2026 QMK Contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay lighting key events and decode real nRF5 PWM output without hardware."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class LightingTests(unittest.TestCase):
    def setUp(self):
        self.cc = shutil.which('cc')
        if not self.cc:
            self.skipTest('Native C compiler is required')
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)

    def write(self, name, text):
        path = self.work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def run_native(self, sources, flags):
        binary = self.work / 'test'
        commands = [
            [self.cc, '-std=gnu11', '-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
             '-I', str(self.work), '-I', str(ROOT / 'quantum'),
             '-I', str(ROOT / 'quantum/process_keycode'), '-I', str(ROOT / 'platforms/nrf5/features'),
             *flags, *[str(ROOT / source) for source in sources], str(self.work / 'test.c'), '-o', str(binary)],
            [str(binary)],
        ]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True,
                                    env={**os.environ, 'ASAN_OPTIONS': 'detect_stack_use_after_return=1'})
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_key_edges_independent_controls_and_legacy_modes(self):
        self.write('action.h', '#pragma once\n#include <stdbool.h>\ntypedef struct { struct { bool pressed; } event; } keyrecord_t;\n')
        operations = ['step', 'step_reverse', 'increase_hue', 'decrease_hue', 'increase_sat', 'decrease_sat',
                      'increase_val', 'decrease_val', 'increase_speed', 'decrease_speed', 'flags_step', 'flags_step_reverse']
        declarations = []
        definitions = []
        for prefix, state, changes in [('rgb_matrix', 'matrix', 'matrix_changes'), ('rgblight', 'under', 'under_changes')]:
            for operation in operations:
                declarations.append(f'void {prefix}_{operation}(void);')
                definitions.append(f'void {prefix}_{operation}(void) {{ {changes}++; }}')
            declarations += [f'void {prefix}_enable(void);', f'void {prefix}_disable(void);',
                             f'void {prefix}_toggle(void);', f'bool {prefix}_is_enabled(void);']
            definitions += [f'void {prefix}_enable(void) {{ {state} = true; {changes}++; }}',
                            f'void {prefix}_disable(void) {{ {state} = false; {changes}++; }}',
                            f'void {prefix}_toggle(void) {{ {state} = !{state}; {changes}++; }}',
                            f'bool {prefix}_is_enabled(void) {{ return {state}; }}']
        mode_names = ['STATIC_LIGHT', 'BREATHING', 'RAINBOW_MOOD', 'RAINBOW_SWIRL', 'SNAKE', 'KNIGHT',
                      'CHRISTMAS', 'STATIC_GRADIENT', 'RGB_TEST', 'TWINKLE']
        modes = '\n'.join(f'#define RGBLIGHT_MODE_{name} {i+1}\n#define RGBLIGHT_MODE_{name}_end {i+2}' for i, name in enumerate(mode_names))
        self.write('quantum.h', '#pragma once\n#include <stdint.h>\n#include "action.h"\n#include "keycodes.h"\n' +
                   '\n'.join(declarations) + '\nuint32_t timer_read32(void);\nuint8_t rgb_matrix_get_mode(void);\nuint8_t rgb_matrix_get_val(void);\nuint8_t rgblight_get_val(void);\nvoid rgblight_mode(uint8_t mode);\nuint8_t rgblight_get_mode(void);\n' + modes)
        self.write('rgb_matrix.h', '#include "quantum.h"\n')
        self.write('rgblight.h', '#include "quantum.h"\n')
        self.write('nrf5/i2c_master.h', 'void i2c_start(void);\nvoid i2c_stop(void);\n')
        self.write('action_util.h', '#include <stdint.h>\nuint8_t get_mods(void);\n')
        self.write('adc.h', '#include <stdint.h>\nuint16_t get_vcc(void);\n')
        self.write('nrf_gpio.h', '#include <stdint.h>\nuint32_t nrf_gpio_pin_out_read(uint32_t);\n')
        self.write('modifiers.h', '#define MOD_MASK_SHIFT 0x22\n')
        self.write('test.c', r'''
#include <assert.h>
#include "nrf5_lighting.h"
#include "process_underglow.h"
#include "process_rgb_matrix.h"
static bool matrix = true, under = true, hardware = true;
static unsigned matrix_changes, under_changes, starts, stops, mode_calls;
static uint8_t last_mode;
uint32_t timer_read32(void) { return 1024; }
uint16_t get_vcc(void) { return 3800; }
uint32_t nrf_gpio_pin_out_read(uint32_t pin) { assert(pin == 8); return !under; }
uint8_t rgb_matrix_get_mode(void) { return 1; }
uint8_t rgb_matrix_get_val(void) { return 64; }
uint8_t rgblight_get_val(void) { return 96; }
void nrf5_lighting_diagnostics(uint8_t data[32]);
void nrf5_lighting_control_trace(uint8_t, uint8_t);
uint8_t get_mods(void) { return 0; }
void i2c_start(void) { hardware = true; starts++; }
void i2c_stop(void) { hardware = false; stops++; }
void rgblight_mode(uint8_t mode) { last_mode = mode; mode_calls++; }
uint8_t rgblight_get_mode(void) { return last_mode; }
''' + '\n'.join(definitions) + r'''
static void event(uint16_t key, bool pressed) {
    keyrecord_t record = {.event.pressed = pressed};
    if (nrf5_process_lighting(key, &record, QK_USER_0) && process_underglow(key, &record))
        process_rgb_matrix(key, &record);
}
static void tap(uint16_t key) { event(key, true); event(key, false); }
int main(void) {
    event(RM_TOGG, true);
    assert(matrix && hardware && under && stops == 0);
    event(RM_TOGG, false);
    assert(!matrix && hardware && under && stops == 0);
    tap(RM_TOGG);
    assert(matrix && hardware && starts == 0 && under);
    event(UG_TOGG, true);
    assert(matrix && hardware && under);
    event(UG_TOGG, false);
    assert(matrix && hardware && !under);
    tap(QK_USER_0); // Mixed on/off states must become all off, not swap.
    assert(!matrix && !under && hardware);
    tap(QK_USER_0);
    assert(matrix && under && hardware);
    unsigned m = matrix_changes, u = under_changes;
    tap(UG_HUEU);
    assert(matrix_changes == m && under_changes == u + 1);
    u = under_changes;
    tap(RM_HUEU);
    assert(matrix_changes == m + 1 && under_changes == u);
    tap(RGB_M_B);
    assert(last_mode == RGBLIGHT_MODE_BREATHING && mode_calls == 1);
    u = under_changes;
    tap(RGB_M_B);
    assert(mode_calls == 1 && under_changes == u + 1);
    tap(RGB_M_SW);
    assert(last_mode == RGBLIGHT_MODE_RAINBOW_SWIRL && mode_calls == 2);
    uint8_t diagnostics[32] = {0x85};
    nrf5_lighting_control_trace(0,0); // Polling never counts as a write.
    nrf5_lighting_control_trace(2,0); // Observation does not itself apply a change.
    nrf5_lighting_diagnostics(diagnostics);
    assert(diagnostics[0] == 0x85 && diagnostics[1] == 1 && diagnostics[2] == 3);
    assert(diagnostics[3] == 0 && diagnostics[6] == 4 && diagnostics[7] == 0);
    assert(diagnostics[8] == (3800 >> 8) && diagnostics[9] == (3800 & 255));
    assert(diagnostics[10] == 0 && diagnostics[11] == 5 && diagnostics[12] == 0 && diagnostics[13] == 5);
    assert(diagnostics[14] == 0 && diagnostics[15] == 1 && diagnostics[18] == 0);
    assert(diagnostics[19] == 2 && diagnostics[24] == 0 && matrix && under);
    for (unsigned i = 25; i < 32; ++i) assert(diagnostics[i] == 0);
    return 0;
}
''')
        effects = ['BREATHING', 'RAINBOW_MOOD', 'RAINBOW_SWIRL', 'SNAKE', 'KNIGHT', 'CHRISTMAS', 'STATIC_GRADIENT', 'RGB_TEST', 'TWINKLE']
        self.run_native(['platforms/nrf5/features/lighting.c', 'quantum/process_keycode/process_underglow.c',
                         'quantum/process_keycode/process_rgb_matrix.c'],
                        ['-DRGBLIGHT_ENABLE', '-DRGB_MATRIX_ENABLE', '-DRGB_MATRIX_IS31FL3741', '-DNRF5_LIGHTING_DIAGNOSTICS', '-DRGB_PWR_PIN=8',
                         '-DRGB_MATRIX_DISABLE_SHARED_KEYCODES',
                         *['-DRGBLIGHT_EFFECT_' + effect for effect in effects]])

    def check_pwm(self, rgbw):
        self.write('nrfx_pwm.h', r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint16_t nrf_pwm_values_common_t;
typedef struct { int id; } nrfx_pwm_t;
typedef struct { uint8_t output_pins[4]; int base_clock, count_mode, load_mode, step_mode; uint16_t top_value; } nrfx_pwm_config_t;
typedef struct { union { const uint16_t *p_common; } values; uint16_t length, repeats, end_delay; } nrf_pwm_sequence_t;
#define NRFX_PWM_INSTANCE(index) {.id = index}
#define NRFX_PWM_DEFAULT_CONFIG {0}
#define NRFX_PWM_PIN_NOT_USED 255
#define NRF_PWM_CLK_16MHz 0
#define NRF_PWM_MODE_UP 0
#define NRF_PWM_LOAD_COMMON 0
#define NRF_PWM_STEP_AUTO 0
#define NRFX_SUCCESS 0
#define NRFX_PWM_FLAG_STOP 1
int nrfx_pwm_init(const nrfx_pwm_t *, const nrfx_pwm_config_t *, void (*handler)(int));
uint32_t nrfx_pwm_simple_playback(const nrfx_pwm_t *, const nrf_pwm_sequence_t *, uint16_t, uint32_t);
bool nrfx_pwm_is_stopped(const nrfx_pwm_t *);
''')
        self.write('test.c', r'''
#include <assert.h>
#include "nrfx_pwm.h"
#include "drivers/nrf5/ws2812.h"
static const uint16_t *samples;
static unsigned count, frames, polls, inits;
int nrfx_pwm_init(const nrfx_pwm_t *pwm, const nrfx_pwm_config_t *config, void (*handler)(int)) {
    assert(handler == NULL && config->output_pins[0] == 6);
    assert(config->base_clock == NRF_PWM_CLK_16MHz && config->top_value == 20);
    assert(config->load_mode == NRF_PWM_LOAD_COMMON && config->step_mode == NRF_PWM_STEP_AUTO);
    inits++; return 0;
}
uint32_t nrfx_pwm_simple_playback(const nrfx_pwm_t *pwm, const nrf_pwm_sequence_t *sequence, uint16_t repeats, uint32_t flags) {
    assert(repeats == 1 && flags == NRFX_PWM_FLAG_STOP);
    samples = sequence->values.p_common; count = sequence->length; frames++; polls = 0; return 0;
}
bool nrfx_pwm_is_stopped(const nrfx_pwm_t *pwm) { return ++polls > 1; }
static uint8_t byte(unsigned index) {
    uint8_t value = 0;
    for (unsigned bit = 0; bit < 8; bit++) {
        uint16_t pulse = samples[index * 8 + bit];
        assert(pulse == (0x8000 | 6) || pulse == (0x8000 | 12));
        value = (value << 1) | ((pulse & 0x7FFF) == 12);
    }
    return value;
}
int main(void) {
    ws2812_init();
    ws2812_set_color_all(80, 60, 40);
    ws2812_set_color(17, 0xA5, 0x5A, 0x0F);
    ws2812_set_color(-1, 0, 0, 0);
    ws2812_set_color(18, 0, 0, 0);
    ws2812_flush();
#ifdef WS2812_RGBW
    const unsigned channels = 4;
#else
    const unsigned channels = 3;
#endif
    // All 18 pixels, including the lower half, must be in the transmitted frame.
    assert(count == 18 * channels * 8 + 64 * 24 + 224);
    for (unsigned i = 0; i < 17; i++) {
#ifdef WS2812_RGBW
        assert(byte(i * channels) == 20 && byte(i * channels + 1) == 40 && byte(i * channels + 2) == 0);
        assert(byte(i * channels + 3) == 40);
#else
        assert(byte(i * channels) == 60 && byte(i * channels + 1) == 80 && byte(i * channels + 2) == 40);
#endif
    }
#ifdef WS2812_RGBW
    assert(byte(17 * channels) == 0x4B && byte(17 * channels + 1) == 0x96 && byte(17 * channels + 2) == 0);
    assert(byte(17 * channels + 3) == 0x0F);
#else
    assert(byte(17 * channels) == 0x5A && byte(17 * channels + 1) == 0xA5 && byte(17 * channels + 2) == 0x0F);
#endif
    for (unsigned i = 18 * channels * 8; i < count - 224; i++) assert(samples[i] == (0x8000 | 6));
    for (unsigned i = count - 224; i < count; i++) assert(samples[i] == 0x8000);
    ws2812_set_color_all(0, 0, 0);
    ws2812_flush();
    for (unsigned i = 0; i < 18 * channels; i++) assert(byte(i) == 0);
    assert(inits == 1 && frames == 2 && polls >= 2);
    return 0;
}
''')
        self.run_native(['drivers/nrf5/ws2812_pwm.c', 'drivers/led/ws2812.c'],
                        ['-I', str(ROOT), '-DRGBLIGHT_WS2812', '-DRGBLIGHT_LED_COUNT=18', '-DWS2812_DI_PIN=6',
                         '-DWS2812_T0H=375', '-DWS2812_T1H=750',
                         *(['-DWS2812_RGBW'] if rgbw else [])])

    def test_pwm_transmits_all_18_pixels_and_latch_interval(self):
        self.check_pwm(False)

    def test_pwm_rgbw_frame(self):
        self.check_pwm(True)

    def test_actual_rgblight_caps_layer_restores_all_pixels(self):
        self.write('nrf_gpio.h', '''#pragma once
#include <stdint.h>
void nrf_gpio_pin_write(uint32_t, uint32_t);
void nrf_gpio_cfg_output(uint32_t);
uint32_t nrf_gpio_pin_out_read(uint32_t);
''')
        self.write('app_util.h', '#pragma once\n#define MIN(a,b) ((a)<(b)?(a):(b))\n')
        self.write('wait.h', 'void wait_us(unsigned);\n')
        self.write('progmem.h', '''#pragma once
#include <stdint.h>
#include <string.h>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_ptr(p) (*(p))
#define memcpy_P memcpy
''')
        self.write('debug.h', '#define dprintf(...) ((void)0)\n')
        self.write('sync_timer.h', '''#include <stdint.h>
extern uint16_t test_time;
static inline uint16_t sync_timer_read(void) { return test_time; }
#define timer_expired(now, last) ((int16_t)((now) - (last)) >= 0)
''')
        self.write('eeconfig.h', '''#pragma once
#include "rgblight.h"
void eeconfig_read_rgblight(rgblight_config_t *);
void eeconfig_update_rgblight(const rgblight_config_t *);
''')
        keymap = (ROOT / 'keyboards/ciank67/keymaps/default/keymap.c').read_text()
        caps_code = keymap.split('const rgblight_segment_t PROGMEM my_capslock_layer[]', 1)[1].split('#endif', 1)[0]
        self.write('test.c', r'''
#include <assert.h>
#include <stddef.h>
#include "rgblight.h"
typedef struct { bool caps_lock; } led_t;
const rgblight_segment_t PROGMEM my_capslock_layer[]''' + caps_code + r'''
static rgb_t pixels[RGBLIGHT_LED_COUNT];
static bool power;
uint16_t test_time;
static unsigned settling, frames, power_ready_at;
void wait_us(unsigned us) { assert(us == 2000 && power); test_time += us / 1000; settling++; }
uint32_t nrf_gpio_pin_out_read(uint32_t pin) { assert(pin == 8); return !power; }
void nrf_gpio_pin_write(uint32_t pin, uint32_t value) { assert(pin == 8); if (!value && !power) power_ready_at = test_time + 2; power = !value; }
void nrf_gpio_cfg_output(uint32_t pin) { assert(pin == 8); }
static void init(void) {}
static void flush(void) { if (power) assert((int16_t)(test_time - power_ready_at) >= 0); frames++; }
static void color(int i, uint8_t r, uint8_t g, uint8_t b) {
    assert(i >= 0 && i < RGBLIGHT_LED_COUNT);
    pixels[i] = (rgb_t){r, g, b};
}
static void all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < RGBLIGHT_LED_COUNT; i++) color(i, r, g, b);
}
const rgblight_driver_t rgblight_driver = {init, color, all, flush};
void eeconfig_read_rgblight(rgblight_config_t *config) {
    *config = (rgblight_config_t){.enable = 1, .mode = RGBLIGHT_MODE_STATIC_LIGHT,
                               .hue = 0, .sat = 255, .val = 64};
}
void eeconfig_update_rgblight(const rgblight_config_t *config) {}
static void check(bool caps, bool enabled) {
    led_update_user((led_t){.caps_lock = caps});
    rgblight_task();
    assert(power == (enabled || caps));
    assert(rgblight_is_enabled() == enabled);
    for (unsigned i = 0; i < RGBLIGHT_LED_COUNT; i++) {
        hsv_t hsv = caps && i >= 9 && i < 17 ? (hsv_t){HSV_WHITE} :
                    enabled ? (hsv_t){0, 255, 64} : (hsv_t){0, 0, 0};
        rgb_t expected = hsv_to_rgb(hsv);
        assert(pixels[i].r == expected.r);
        assert(pixels[i].g == expected.g);
        assert(pixels[i].b == expected.b);
    }
}
int main(void) {
    rgblight_init();
    rgblight_layers = my_rgb_layers;
    check(false, true);
    rgblight_disable_noeeprom();
    assert(!power);
    check(true, false);
    // Power-up must settle once. A lost first frame is followed by two
    // bounded refreshes without toggling power or enabling the base effect.
    unsigned previous_frames = frames, previous_settling = settling;
    test_time += 19; rgblight_task(); assert(frames == previous_frames);
    test_time++; rgblight_task(); assert(frames == previous_frames + 1);
    test_time += 20; rgblight_task(); assert(frames == previous_frames + 2);
    test_time += 200; rgblight_task(); assert(frames == previous_frames + 2);
    assert(settling == previous_settling);
    check(false, false);
    // The next retry must render the new OFF state, never replay stale white.
    test_time += 20; rgblight_task(); assert(!power);
    for (unsigned i = 0; i < RGBLIGHT_LED_COUNT; i++) assert(!pixels[i].r && !pixels[i].g && !pixels[i].b);
    rgblight_enable_noeeprom();
    check(true, true);
    rgblight_disable_noeeprom(); // Caps already on when the base is disabled.
    assert(power);
    check(true, false);
    check(false, false);
    rgblight_enable_noeeprom();
    check(false, true);
    rgblight_disable_noeeprom();
    check(true, false);
    check(false, false);
    rgblight_enable_noeeprom();
    check(false, true);
}
''')
        self.run_native(['quantum/rgblight/rgblight.c', 'quantum/color.c'],
                        ['-I', str(ROOT / 'quantum/rgblight'), '-I', str(ROOT),
                         '-DRGBLIGHT_ENABLE', '-DRGBLIGHT_LED_COUNT=18', '-DRGBLIGHT_LAYERS',
                         '-DRGBLIGHT_LAYERS_OVERRIDE_RGB_OFF', '-DRGB_PWR_PIN=8', '-DPROTOCOL_NRF5',
                         '-include', str(self.work / 'nrf_gpio.h')])

    def test_underglow_selftest_patterns_and_restore(self):
        self.write('quantum.h', '''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { bool pressed; } keyrecord_t;
typedef struct { uint8_t index, count, hue, sat, val; } rgblight_segment_t;
extern const rgblight_segment_t *const *rgblight_layers;
bool rgblight_is_enabled(void);
uint8_t rgblight_get_mode(void);
void rgblight_mode_noeeprom(uint8_t);
void rgblight_timer_disable(void);
void rgblight_set(void);
void rgb_pwr_on(void);
void rgb_pwr_off(void);
uint32_t timer_read32(void);
uint32_t timer_elapsed32(uint32_t);
uint16_t timer_read(void);
uint16_t timer_elapsed(uint16_t);
''')
        self.write('nrf5/ws2812.h', '''#pragma once
#include "color.h"
void ws2812_setleds(rgb_led_t *, uint16_t);
''')
        self.write('test.c', r'''
#include <assert.h>
#include "nrf5_lighting.h"
#include "nrf5/ws2812.h"
static const rgblight_segment_t segment = {9, 8, 0, 0, 255};
static const rgblight_segment_t *const layers[] = {&segment, NULL};
const rgblight_segment_t *const *rgblight_layers = layers;
static bool enabled = true, power = true;
static uint32_t now;
static unsigned frames, restores, phase;
bool rgblight_is_enabled(void) { return enabled; }
uint8_t rgblight_get_mode(void) { return 5; }
void rgblight_mode_noeeprom(uint8_t mode) {
    assert(mode == 5 && rgblight_layers == layers); restores++;
}
void rgblight_timer_disable(void) {}
void rgb_pwr_on(void) { power = true; }
void rgb_pwr_off(void) { power = false; }
void rgblight_set(void) { assert(!enabled); power = false; }
uint32_t timer_read32(void) { return now; }
uint32_t timer_elapsed32(uint32_t t) { return now - t; }
uint16_t timer_read(void) { return now; }
uint16_t timer_elapsed(uint16_t t) { return (uint16_t)(now - t); }
void ws2812_setleds(rgb_led_t *pixels, uint16_t count) {
    assert(count == 18);
    for (unsigned i = 0; i < count; i++) {
        assert(pixels[i].r == ((phase == 0 || (phase == 3 && i >= 9 && i < 17)) ? 64 : 0));
        assert(pixels[i].g == ((phase == 1 && i < 9) || (phase == 3 && i >= 9 && i < 17) ? 64 : 0));
        assert(pixels[i].b == ((phase == 2 && i >= 9) || (phase == 3 && i >= 9 && i < 17) ? 64 : 0));
    }
    frames++;
}
int main(void) {
    nrf5_underglow_test_task();
    assert(frames == 0);
    nrf5_underglow_test();
    assert(rgblight_layers == NULL);
    for (phase = 0; phase < 4; phase++) {
        now = phase * 2000;
        nrf5_underglow_test_task();
        assert(frames == phase + 1);
        nrf5_underglow_test_task(); // No duplicate frame before 20 ms.
        assert(frames == phase + 1);
    }
    now = 8000;
    nrf5_underglow_test_task();
    assert(restores == 1 && rgblight_layers == layers);
    nrf5_underglow_test();
    nrf5_underglow_test(); // Press again to cancel.
    assert(restores == 2 && rgblight_layers == layers);
    enabled = power = false;
    nrf5_underglow_test();
    assert(power);
    now += 8000;
    phase = 4; // Disabled restoration must transmit black and cut power.
    nrf5_underglow_test_task();
    assert(!enabled && !power && restores == 2 && frames == 4);
    assert(rgblight_layers == layers);
}
''')
        self.run_native(['platforms/nrf5/features/underglow_test.c'], ['-DRGBLIGHT_LED_COUNT=18', '-DRGBLIGHT_ENABLE', '-DRGBLIGHT_LAYERS', '-DRGBLIGHT_WS2812', '-DRGB_PWR_PIN=8', '-DNRF5_UNDERGLOW_TEST_WHITE_COUNT=8'])
