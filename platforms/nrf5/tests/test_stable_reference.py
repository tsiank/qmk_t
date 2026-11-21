# Copyright 2026 QMK Contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare real driver output with stable and check the real nRF main loop."""

import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class StableReferenceTests(unittest.TestCase):
    def setUp(self):
        self.cc = shutil.which('cc')
        if not self.cc:
            self.skipTest('Native C compiler is required')
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)

    def write(self, name, text):
        path = self.work / name
        path.write_text(text)
        return path

    def run_command(self, command):
        result = subprocess.run(command, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        return result.stdout

    def test_pwm_matches_stable_prefix_sample_for_sample(self):
        result = subprocess.run(['git', 'show', 'origin/qmk_tsiank_stable:drivers/nrf5/ws2812.c'],
                                cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            self.skipTest('origin/qmk_tsiank_stable is required for the reference comparison')
        stable = self.write('stable_pwm.c', result.stdout)
        self.write('ws2812.h', f'#include "{ROOT}/drivers/nrf5/ws2812.h"\n')
        for header in ('wait.h', 'nrf_log.h', 'nrf_log_ctrl.h', 'nrfx_power.h'):
            self.write(header, '')
        self.write('nrfx_pwm.h', r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int nrfx_pwm_evt_type_t;
typedef uint16_t nrf_pwm_values_common_t;
typedef struct { int id; } nrfx_pwm_t;
typedef struct { uint8_t output_pins[4]; int base_clock, count_mode, load_mode, step_mode; uint16_t top_value; } nrfx_pwm_config_t;
typedef union { uint16_t *p_common; } nrf_pwm_values_t;
typedef struct { nrf_pwm_values_t values; uint16_t length, repeats, end_delay; } nrf_pwm_sequence_t;
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
        harness = self.write('pwm_test.c', r'''
#include <assert.h>
#include <stdio.h>
#include "nrfx_pwm.h"
#include "ws2812.h"
int nrfx_pwm_init(const nrfx_pwm_t *p, const nrfx_pwm_config_t *c, void (*handler)(int)) {
    assert(c->top_value == 20 && c->base_clock == NRF_PWM_CLK_16MHz);
    assert(c->load_mode == NRF_PWM_LOAD_COMMON); return 0;
}
uint32_t nrfx_pwm_simple_playback(const nrfx_pwm_t *p, const nrf_pwm_sequence_t *s, uint16_t times, uint32_t flags) {
    assert(times == 1 && flags == NRFX_PWM_FLAG_STOP);
    assert(fwrite(s->values.p_common, sizeof(uint16_t), s->length, stdout) == s->length);
    return 0;
}
bool nrfx_pwm_is_stopped(const nrfx_pwm_t *p) { return true; }
int main(void) {
    rgb_led_t pixels[18];
    for (unsigned i = 0; i < 18; i++) {
        pixels[i].r = 17 * i + 3; pixels[i].g = 255 - 7 * i; pixels[i].b = 1 << (i % 8);
    }
    ws2812_setleds(pixels, 18);
    return 0;
}
''')
        outputs = []
        for name, source in [('stable', stable), ('current', ROOT / 'drivers/nrf5/ws2812_pwm.c')]:
            binary = self.work / name
            self.run_command([self.cc, '-std=gnu11', '-I', str(self.work), '-I', str(ROOT / 'quantum'),
                              '-DLED_TYPE=rgb_led_t', '-DRGBLED_NUM=18', '-DRGB_DI_PIN=6',
                              '-DRGBLIGHT_WS2812', '-DWS2812_DI_PIN=6', '-DWS2812_T0H=375', '-DWS2812_T1H=750',
                              str(source), str(harness), '-o', str(binary)])
            outputs.append(self.run_command([str(binary)]))
        reference, current = outputs
        self.assertEqual(len(reference), (18 * 24 + 64 * 24 + 8) * 2)
        self.assertEqual(current[:len(reference)], reference)
        extra = current[len(reference):]
        self.assertEqual(extra, struct.pack('<H', 0x8000) * (224 - 8))

    def test_timer_irq_only_schedules_main_thread_work(self):
        self.write('sdk.h', r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef unsigned ret_code_t;
#define BLE_CONN_HANDLE_INVALID 0xFFFF
#define APP_TIMER_DEF(name) static int name
#define APP_TIMER_MODE_REPEATED 1
#define APP_TIMER_TICKS(value) (value)
#define UNUSED_PARAMETER(value) (void)(value)
#define APP_ERROR_CHECK(value) do { if (value) abort(); } while (0)
#define NRF_LOG_INIT(value) 0
#define NRF_LOG_DEFAULT_BACKENDS_INIT() ((void)0)
typedef struct { unsigned ENABLE; } usbd_t;
extern usbd_t usbd;
#define NRF_USBD (&usbd)
unsigned app_timer_init(void);
unsigned app_timer_create(int *, int, void (*handler)(void *));
unsigned app_timer_start(int, unsigned, void *);
bool app_usbd_event_queue_process(void);
void keyboard_setup(void);
void keyboard_init(void);
void keyboard_task(void);
void eeprom_update(void);
void housekeeping_task(void);
void usb_keyboard_init(void);
void ble_service_init(void);
void advertising_start(bool);
void idle_state_handle(void);
''')
        for header in ('nrf_log_ctrl.h', 'nrf_log_default_backends.h', 'nrf5_usb.h', 'ble_service.h',
                       'ble.h', 'keyboard.h', 'eeprom.h'):
            self.write(header, '#include "sdk.h"\n')
        self.write('nrf5_power.h', '#include <stdbool.h>\nstatic inline bool nrf5_usb_powered(void){return true;}\nvoid nrf5_power_task(void);\n')
        harness = self.write('main_test.c', r'''
#include <assert.h>
#include "sdk.h"
usbd_t usbd = {.ENABLE = 1};
static void (*tick)(void *);
static unsigned irq, tasks, writes, housekeeping, queue_calls, idle_calls, started, ble_ready, display_calls;
unsigned app_timer_init(void) { return 0; }
unsigned app_timer_create(int *timer, int mode, void (*handler)(void *)) { *timer = 1; tick = handler; return 0; }
unsigned app_timer_start(int timer, unsigned interval, void *context) { assert(interval == 4);started=1;return 0; }
void keyboard_setup(void) {}
void keyboard_init(void) {}
void usb_keyboard_init(void) {}
void ble_service_init(void) {ble_ready=1;}
uint32_t timer_read32(void) {return tasks*1000;}
uint32_t timer_elapsed32(uint32_t at){return timer_read32()-at;}
bool oled_init(int rotation){assert(started&&ble_ready&&irq==0&&queue_calls>0);display_calls++;return display_calls>1;}
void nrf5_power_task(void){assert(irq==0);}
void advertising_start(bool erase) {}
void keyboard_task(void) { assert(irq == 0); tasks++; }
void eeprom_update(void) { assert(irq == 0); writes++; }
void housekeeping_task(void) { assert(irq == 0); housekeeping++; }
bool app_usbd_event_queue_process(void) {
    if (++queue_calls == 1) {
        irq = 1; tick(NULL); tick(NULL);
        assert(tasks == 0 && writes == 0); // ISR must not touch lighting or flash.
        irq = 0;
    }
    return false;
}
void idle_state_handle(void) {
    assert(tasks == writes && tasks == housekeeping);
    if (++idle_calls == 1) {
        assert(tasks == 1); // Coalesce missed ticks instead of starving USB.
        irq = 1; tick(NULL); irq = 0;
    } else {
        assert(tasks == 2);
#ifdef OLED_ENABLE
        assert(display_calls==2); // Initial failure is retried after RTC starts.
#endif
        exit(0);
    }
}
int nrf5_application_main(void);
int main(void) { nrf5_application_main(); return 1; }
''')
        obj = self.work / 'main.o'
        binary = self.work / 'main_test'
        self.run_command([self.cc, '-std=c11', '-I', str(self.work), '-Dmain=nrf5_application_main',
                          '-c', str(ROOT / 'tmk_core/protocol/nrf5/main.c'), '-o', str(obj)])
        self.run_command([self.cc, '-std=c11', '-I', str(self.work), str(obj), str(harness), '-o', str(binary)])
        self.run_command([str(binary)])
        self.write('oled_driver.h', '#include "sdk.h"\n#define OLED_ROTATION_0 0\nbool oled_init(int);\n')
        self.write('timer.h', '#include <stdint.h>\nuint32_t timer_read32(void);uint32_t timer_elapsed32(uint32_t);\n')
        self.run_command([self.cc, '-std=c11', '-I', str(self.work), '-DOLED_ENABLE', '-Dmain=nrf5_application_main',
                          '-c', str(ROOT / 'tmk_core/protocol/nrf5/main.c'), '-o', str(obj)])
        self.run_command([self.cc, '-std=c11', '-I', str(self.work), '-DOLED_ENABLE', str(obj), str(harness), '-o', str(binary)])
        self.run_command([str(binary)])
