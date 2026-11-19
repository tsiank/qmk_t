# Copyright 2026 QMK Contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the real stream parser, transport callbacks and renderer without hardware."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class MusicTests(unittest.TestCase):
    def setUp(self):
        self.cc = shutil.which('cc')
        if not self.cc:
            self.skipTest('Native compiler required')
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        self.write('app_util_platform.h', '''#pragma once
#include <assert.h>
extern unsigned critical_depth;
#define CRITICAL_REGION_ENTER() do { assert(critical_depth++ == 0); } while (0)
#define CRITICAL_REGION_EXIT() do { assert(--critical_depth == 0); } while (0)
''')
        self.write('timer.h', '''#pragma once
#include <stdint.h>
uint32_t timer_read32(void);
uint32_t timer_elapsed32(uint32_t);
''')

    def write(self, name, text):
        (self.work / name).write_text(text)

    def run_native(self, sources):
        command = [self.cc, '-std=gnu11', '-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                   '-I', str(self.work), '-I', str(ROOT / 'platforms/nrf5'),
                   '-I', str(ROOT / 'keyboards/ciank67'), '-I', str(ROOT / 'quantum/rgblight'),
                   *[str(ROOT / source) for source in sources], str(self.work / 'test.c'),
                   '-o', str(self.work / 'test')]
        for cmd in [command, [str(self.work / 'test')]]:
            result = subprocess.run(cmd, capture_output=True, text=True,
                                    env={**os.environ, 'ASAN_OPTIONS': 'detect_stack_use_after_return=1'})
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_packets_timeout_disconnect_and_timer_wrap(self):
        self.write('test.c', r'''
#include <assert.h>
#include <string.h>
#include "music_stream.h"
unsigned critical_depth;
static uint32_t now;
uint32_t timer_read32(void) { return now; }
uint32_t timer_elapsed32(uint32_t t) { return now - t; }
int main(void) {
    music_frame_t frame;
    uint8_t data[20] = {0xC7, 1, 255, MUSIC_MATRIX | MUSIC_UNDERGLOW};
    for (unsigned i = 0; i < 16; i++) data[i + 4] = i * 17;
    assert(!music_stream_read(&frame));
    assert(!music_stream_receive(NULL, 20, MUSIC_SOURCE_USB));
    assert(!music_stream_receive(data, 19, MUSIC_SOURCE_USB));
    assert(!music_stream_receive(data, 21, MUSIC_SOURCE_USB));
    assert(!music_stream_receive(data, 20, 7));
    data[0] = 0; assert(!music_stream_receive(data, 20, MUSIC_SOURCE_USB)); data[0] = 0xC7;
    data[1] = 2; assert(!music_stream_receive(data, 20, MUSIC_SOURCE_USB)); data[1] = 1;
    data[3] = 4; assert(!music_stream_receive(data, 20, MUSIC_SOURCE_USB)); data[3] = 3;
    now = UINT32_MAX - 100;
    assert(music_stream_receive(data, 20, MUSIC_SOURCE_USB));
    memset(data + 4, 0, 16); // The callback must own a snapshot, not the SDK buffer.
    assert(music_stream_read(&frame) && frame.bands[15] == 255 && frame.sequence == 255);
    music_stream_disconnect(MUSIC_SOURCE_BLE);
    assert(music_stream_read(&frame));
    now += 749; assert(music_stream_read(&frame));
    now += 1; assert(!music_stream_read(&frame));
    data[2] = 0;
    assert(music_stream_receive(data, 20, MUSIC_SOURCE_BLE));
    assert(music_stream_read(&frame) && frame.sequence == 0);
    music_stream_disconnect(MUSIC_SOURCE_USB); assert(music_stream_read(&frame));
    music_stream_disconnect(MUSIC_SOURCE_BLE); assert(!music_stream_read(&frame));
    data[3] = 1; assert(music_stream_receive(data, 20, MUSIC_SOURCE_USB));
    data[3] = 0; assert(music_stream_receive(data, 20, MUSIC_SOURCE_USB));
    assert(!music_stream_read(&frame) && critical_depth == 0);
}
''')
        self.run_native(['platforms/nrf5/music_stream.c'])

    def test_renderer_independent_off_selftest_and_restore(self):
        self.write('quantum.h', '''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "timer.h"
#define RGB_MATRIX_LED_COUNT 4
#define RGBLIGHT_LED_COUNT 18
#define RGB_MATRIX_CUSTOM_MUSIC_SPECTRUM 42
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef struct {} keyrecord_t;
typedef struct { uint8_t h,s,v; } hsv_t;
typedef struct { uint8_t r,g,b; } rgb_t;
typedef struct { struct { uint8_t x,y; } point[4]; uint8_t flags[4]; } led_config_t;
extern led_config_t g_led_config;
rgb_t hsv_to_rgb(hsv_t);
uint8_t rgb_matrix_get_hue(void);
uint8_t rgb_matrix_get_sat(void);
uint8_t rgb_matrix_get_speed(void);
uint8_t rgb_matrix_get_flags(void);
uint8_t rgb_matrix_override_effect(uint8_t);
bool rgb_matrix_is_enabled(void);
uint8_t rgb_matrix_get_val(void);
void rgb_matrix_set_color(int,uint8_t,uint8_t,uint8_t);
bool rgblight_is_enabled(void);
void rgblight_mode_noeeprom(uint8_t);
uint8_t rgblight_get_mode(void);
uint8_t rgblight_get_val(void);
void rgblight_timer_disable(void);
void rgblight_set(void);
uint16_t timer_read(void);
uint16_t timer_elapsed(uint16_t);
''')
        self.write('test.c', r'''
#include <assert.h>
#include <string.h>
#include "lighting.h"
#include "music.h"
#include "music_stream.h"
#include "rgblight_drivers.h"
unsigned critical_depth;
static uint32_t now;
static bool matrix = true, under = true, selftest;
static unsigned restores, flushes, timer_disables;
static uint8_t matrix_value[4], matrix_sat[4], under_value[18];
led_config_t g_led_config = {.point = {{0,0}, {224,64}, {112,32}, {0,64}}, .flags = {1,1,1,1}};
uint32_t timer_read32(void) { return now; }
uint32_t timer_elapsed32(uint32_t t) { return now-t; }
uint16_t timer_read(void) { return now; }
uint16_t timer_elapsed(uint16_t t) { return (uint16_t)(now-t); }
rgb_t hsv_to_rgb(hsv_t hsv) { return (rgb_t){hsv.h, hsv.s, hsv.v}; }
uint8_t rgb_matrix_get_hue(void) { return 0; }
uint8_t rgb_matrix_get_sat(void) { return 255; }
uint8_t rgb_matrix_get_speed(void) { return 128; }
uint8_t rgb_matrix_get_flags(void) { return 1; }
bool rgb_matrix_is_enabled(void) { return matrix; }
uint8_t rgb_matrix_get_val(void) { return 128; }
void rgb_matrix_set_color(int i,uint8_t r,uint8_t g,uint8_t b) { assert(i>=0 && i<4); matrix_value[i]=b; matrix_sat[i]=g; }
bool rgblight_is_enabled(void) { return under; }
void rgblight_mode_noeeprom(uint8_t mode) { assert(mode==7); restores++; }
uint8_t rgblight_get_mode(void) { return 7; }
uint8_t rgblight_get_val(void) { return 100; }
void rgblight_timer_disable(void) { timer_disables++; }
void rgblight_set(void) { flushes++; }
bool ciank67_underglow_test_is_active(void) { return selftest; }
static void color(int i,uint8_t r,uint8_t g,uint8_t b) { assert(i>=0 && i<18); under_value[i]=b; }
const rgblight_driver_t rgblight_driver = {.set_color=color};
typedef struct { uint8_t iter; bool init; } effect_params_t;
#define RGB_MATRIX_EFFECT(name)
#define RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#define RGB_MATRIX_USE_LIMITS(first,last) uint8_t first=params->iter*2, last=first+2
static bool rgb_matrix_check_finished_leds(uint8_t last) { return last < 4; }
#include "rgb_matrix_kb.inc"
static void draw(bool init) {
    effect_params_t params = {.init=init};
    assert(MUSIC_SPECTRUM(&params)); params.iter++;
    assert(!MUSIC_SPECTRUM(&params));
}
int main(void) {
    uint8_t data[20]={0xC7,1,0,3};
    memset(data+4,255,16);
    assert(rgb_matrix_override_effect(7)==7);
    ciank67_music_task(); assert(flushes==0 && restores==0 && timer_disables==0);
    music_stream_receive(data,20,MUSIC_SOURCE_USB);
    assert(rgb_matrix_override_effect(7)==RGB_MATRIX_CUSTOM_MUSIC_SPECTRUM);
    assert(rgb_matrix_override_effect(0)==0); // Off must never be overridden.
    draw(true); ciank67_music_task();
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==128);
    assert(matrix_sat[0]==255/4 && matrix_sat[3]==255); // White peak on top, colored base.
    for (unsigned i=0;i<18;i++) assert(under_value[i]==100);
    assert(flushes==1);
    ciank67_music_task(); assert(flushes==1);
    under=false; ciank67_music_task(); assert(flushes==2 && restores==0);
    under=true; now+=33; ciank67_music_task(); assert(flushes==3);
    selftest=true; now+=33; ciank67_music_task(); assert(flushes==3);
    selftest=false;
    data[3]=MUSIC_MATRIX;
    music_stream_receive(data,20,MUSIC_SOURCE_USB); ciank67_music_task();
    assert(restores==1); ciank67_music_task(); assert(restores==1);
    matrix=false; memset(matrix_value,17,4); draw(false);
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==17);
    matrix=true;
    // Use a single snapshot for both render batches, even if a packet arrives between them.
    effect_params_t params={.init=true};
    MUSIC_SPECTRUM(&params);
    memset(data+4,0,16); music_stream_receive(data,20,MUSIC_SOURCE_USB);
    params.iter=1; MUSIC_SPECTRUM(&params);
    assert(matrix_value[1]==128 && matrix_value[3]==128);
    now+=100; draw(false);
    assert(matrix_value[0]==128 && matrix_sat[0]==255/4); // Peak hold.
    now+=200; draw(false); assert(matrix_value[0]==0); // Peak starts falling.
    for (unsigned i=0;i<40;i++) { now+=33; draw(false); }
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==0);
    assert(rgb_matrix_override_effect(7)==7); // Timeout returns the configured mode.
    g_led_config.flags[3]=2; matrix_value[3]=17;
    draw(false); assert(matrix_value[3]==17); // Standard LED flags are respected.
    data[3]=3; music_stream_receive(data,20,MUSIC_SOURCE_USB); ciank67_music_task();
    now+=750; ciank67_music_task(); assert(restores==2);
    ciank67_music_task(); assert(restores==2);
}
''')
        self.run_native(['platforms/nrf5/music_stream.c', 'keyboards/ciank67/music.c'])

    def test_actual_matrix_task_override_preserves_off_and_saved_mode(self):
        code = (ROOT / 'quantum/rgb_matrix/rgb_matrix.c').read_text()
        task = code.split('static void rgb_task_start(void) {', 1)[1].split('\nstatic void rgb_task_render', 1)[0]
        self.write('test.c', r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define RGB_MATRIX_TIMEOUT 0
static struct { uint8_t iter; } rgb_effect_params;
static struct { bool enable; uint8_t mode; } rgb_matrix_config = {true, 7};
static uint32_t g_rgb_timer, rgb_timer_buffer = 123;
static bool suspend_state, streaming;
static uint8_t rgb_current_effect, rgb_task_state;
enum { RENDERING = 3 };
static unsigned calls;
static uint8_t rgb_matrix_override_effect(uint8_t effect) {
    calls++;
    return streaming ? 42 : effect;
}
static void rgb_task_start(void) {
''' + task + r'''
int main(void) {
    rgb_task_start();
    assert(rgb_current_effect == 7 && calls == 1);
    assert(g_rgb_timer == 123 && rgb_effect_params.iter == 0 && rgb_task_state == RENDERING);
    streaming = true; rgb_task_start();
    assert(rgb_current_effect == 42 && rgb_matrix_config.mode == 7 && calls == 2);
    suspend_state = true; rgb_task_start();
    assert(rgb_current_effect == 0 && calls == 2);
    suspend_state = false; rgb_matrix_config.enable = false; rgb_task_start();
    assert(rgb_current_effect == 0 && calls == 2 && rgb_matrix_config.mode == 7);
    rgb_matrix_config.enable = true; streaming = false; rgb_task_start();
    assert(rgb_current_effect == 7 && calls == 3);
    rgb_matrix_config.mode = 0; streaming = true; rgb_task_start();
    assert(rgb_current_effect == 0 && calls == 3);
}
''')
        self.run_native([])

    def test_actual_usb_output_callback(self):
        code = (ROOT / 'tmk_core/protocol/nrf5/usb/nrf5_usb.c').read_text()
        callback = code.split('static void music_usb_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event) {', 1)[1].split('\n#endif', 1)[0]
        self.write('test.c', r'''
#include <assert.h>
#include <string.h>
#include "music_stream.h"
unsigned critical_depth;
uint32_t timer_read32(void) { return 0; }
uint32_t timer_elapsed32(uint32_t t) { return 0; }
typedef int app_usbd_class_inst_t;
typedef int app_usbd_hid_user_event_t;
#define APP_USBD_HID_USER_EVT_OUT_REPORT_READY 1
static uint8_t report[21]={4,0xC7,1,0,3};
static size_t size=21;
static const int *app_usbd_hid_generic_class_get(const int *p) { return p; }
static const uint8_t *app_usbd_hid_generic_out_report_get(const int *p, size_t *s) { *s=size; return report; }
static void music_usb_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event) {''' + callback + r'''
int main(void) {
    music_frame_t frame;
    int instance=0;
    music_usb_event(&instance,0); assert(!music_stream_read(&frame));
    size=20; music_usb_event(&instance,1); assert(!music_stream_read(&frame));
    size=21; report[0]=0; music_usb_event(&instance,1); assert(!music_stream_read(&frame));
    report[0]=4; music_usb_event(&instance,1); assert(music_stream_read(&frame));
    assert(frame.flags==3 && frame.source==MUSIC_SOURCE_USB);
}
''')
        self.run_native(['platforms/nrf5/music_stream.c'])

    def test_ble_service_registration_and_write_callback(self):
        self.write('host_driver.h', 'typedef struct {} host_driver_t;\n')
        self.write('nrf_drv_saadc.h', '#include <stdint.h>\ntypedef int16_t nrf_saadc_value_t;\n')
        self.write('ble.h', r'''#pragma once
#include <stdint.h>
#define BLE_GATTS_EVT_WRITE 1
#define BLE_GAP_EVT_DISCONNECTED 2
#define BLE_GATTS_OP_WRITE_REQ 1
#define BLE_GATTS_OP_WRITE_CMD 2
#define BLE_GATTS_SRVC_TYPE_PRIMARY 1
typedef struct { uint8_t uuid128[16]; } ble_uuid128_t;
typedef struct { uint16_t uuid; uint8_t type; } ble_uuid_t;
typedef struct { uint16_t value_handle; } ble_gatts_char_handles_t;
typedef struct { uint16_t handle, offset, len; uint8_t op, data[20]; } ble_gatts_evt_write_t;
typedef struct { struct { uint16_t evt_id; } header; struct { struct { struct { ble_gatts_evt_write_t write; } params; } gatts_evt; } evt; } ble_evt_t;
uint32_t sd_ble_uuid_vs_add(const ble_uuid128_t *, uint8_t *);
uint32_t sd_ble_gatts_service_add(uint8_t, const ble_uuid_t *, uint16_t *);
''')
        self.write('ble_srv_common.h', r'''#include "ble.h"
#define SEC_JUST_WORKS 2
typedef struct { uint16_t uuid, max_len, init_len; uint8_t uuid_type; uint8_t *p_init_value;
struct { unsigned write, write_wo_resp; } char_props; unsigned write_access; } ble_add_char_params_t;
uint32_t characteristic_add(uint16_t, const ble_add_char_params_t *, ble_gatts_char_handles_t *);
''')
        self.write('nrf_sdh_ble.h', '#include <stddef.h>\n#define NRF_SDH_BLE_OBSERVER(name, priority, handler, context)\n')
        self.write('app_error.h', '#include <assert.h>\n#define APP_ERROR_CHECK(code) assert((code) == 0)\n')
        source = ROOT / 'tmk_core/protocol/nrf5/ble/music_gatt.c'
        self.write('test.c', '#include "' + str(source) + '"\n' + r'''
#include <string.h>
unsigned critical_depth;
static unsigned activity;
void reset_power_save_counter(void) { activity++; }
uint32_t timer_read32(void) { return 0; }
uint32_t timer_elapsed32(uint32_t t) { return 0; }
uint32_t sd_ble_uuid_vs_add(const ble_uuid128_t *base, uint8_t *type) {
    assert(base->uuid128[0] == 0 && base->uuid128[1] == 0x67);
    *type = 7; return 0;
}
uint32_t sd_ble_gatts_service_add(uint8_t kind, const ble_uuid_t *uuid, uint16_t *handle) {
    assert(kind == BLE_GATTS_SRVC_TYPE_PRIMARY && uuid->uuid == 1 && uuid->type == 7);
    *handle = 10; return 0;
}
uint32_t characteristic_add(uint16_t service, const ble_add_char_params_t *p, ble_gatts_char_handles_t *handles) {
    assert(service == 10 && p->uuid == 2 && p->uuid_type == 7);
    assert(p->max_len == 20 && p->init_len == 20 && p->p_init_value[0] == 0);
    assert(p->char_props.write && p->char_props.write_wo_resp && p->write_access == SEC_JUST_WORKS);
    handles->value_handle = 11; return 0;
}
int main(void) {
    music_gatt_init();
    music_frame_t frame;
    ble_evt_t event = {.header.evt_id = BLE_GATTS_EVT_WRITE};
    ble_gatts_evt_write_t *write = &event.evt.gatts_evt.params.write;
    write->len = 20; write->op = BLE_GATTS_OP_WRITE_CMD;
    write->data[0] = 0xC7; write->data[1] = 1; write->data[3] = 3;
    music_ble_event(&event, NULL); assert(!music_stream_read(&frame));
    write->handle = 11; write->offset = 1;
    music_ble_event(&event, NULL); assert(!music_stream_read(&frame));
    write->offset = 0; write->op = 9;
    music_ble_event(&event, NULL); assert(!music_stream_read(&frame));
    write->op = BLE_GATTS_OP_WRITE_REQ;
    music_ble_event(&event, NULL); assert(music_stream_read(&frame) && frame.source == MUSIC_SOURCE_BLE);
    assert(activity == 1);
    event.header.evt_id = BLE_GAP_EVT_DISCONNECTED;
    music_ble_event(&event, NULL); assert(!music_stream_read(&frame));
}
''')
        self.run_native(['platforms/nrf5/music_stream.c'])

    def test_sdk_usb_identity_uses_qmk_values_and_honors_explicit_overrides(self):
        header = ROOT / 'tmk_core/protocol/nrf5/pca10056/blank/config/sdk_config.h'
        for mode in ['qmk', 'default', 'override']:
            definitions = '#define APP_USBD_STRING_DESC(text) text\n'
            if mode != 'default':
                definitions += '#define VENDOR_ID 0xFEED\n#define PRODUCT_ID 0xC475\n#define PRODUCT "ciank67"\n#define MANUFACTURER "Tsiank"\n'
            if mode == 'override':
                definitions += '#define APP_USBD_VID 0x1234\n#define APP_USBD_PID 0x5678\n#define APP_USBD_STRINGS_PRODUCT "Custom"\n#define APP_USBD_STRINGS_MANUFACTURER "Owner"\n'
            expected = {'qmk': (0xFEED, 0xC475, 'ciank67', 'Tsiank'),
                        'default': (0x1915, 0x520B, 'nRF52 USB Product', 'Nordic Semiconductor'),
                        'override': (0x1234, 0x5678, 'Custom', 'Owner')}[mode]
            vid, pid, product, manufacturer = expected
            self.write('test.c', definitions + f'''
#include <assert.h>
#include <string.h>
#include "{header}"
int main(void) {{
    assert(APP_USBD_VID == {vid} && APP_USBD_PID == {pid});
    assert(strcmp(APP_USBD_STRINGS_PRODUCT, "{product}") == 0);
    assert(strcmp(APP_USBD_STRINGS_MANUFACTURER, "{manufacturer}") == 0);
}}
''')
            self.run_native([])
