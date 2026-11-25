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
                   '-I', str(ROOT / 'platforms/nrf5/features'), '-I', str(ROOT / 'quantum/rgblight'),
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
    // A short RTC returning to the old timestamp must not revive expired music.
    now = UINT32_MAX - 100; assert(!music_stream_read(&frame));
    now += 750;
    data[2] = 0;
    assert(music_stream_receive(data, 20, MUSIC_SOURCE_BLE));
    assert(music_stream_read(&frame) && frame.sequence == 0);
    music_stream_disconnect(MUSIC_SOURCE_USB); assert(music_stream_read(&frame));
    music_stream_disconnect(MUSIC_SOURCE_BLE); assert(!music_stream_read(&frame));
    data[3] = 1; assert(music_stream_receive(data, 20, MUSIC_SOURCE_USB));
    data[3] = 0; assert(music_stream_receive(data, 20, MUSIC_SOURCE_USB));
    assert(!music_stream_read(&frame) && critical_depth == 0);
    data[3] = 0x13; assert(music_stream_receive(data,20,MUSIC_SOURCE_BLE));
    assert(music_stream_read(&frame) && frame.flags == 3 && frame.effect == MUSIC_EFFECT_DUAL_DANCE);
    assert(!frame.monochrome);
    data[3] = 0x33; assert(music_stream_receive(data,20,MUSIC_SOURCE_BLE));
    assert(music_stream_read(&frame) && frame.flags==3 && frame.monochrome && frame.effect==MUSIC_EFFECT_DUAL_DANCE);
    data[3] = 0x43; assert(!music_stream_receive(data,20,MUSIC_SOURCE_BLE));
    data[3] = 0x08; assert(!music_stream_receive(data,20,MUSIC_SOURCE_BLE));
    assert(music_stream_read(&frame) && frame.effect == MUSIC_EFFECT_DUAL_DANCE);
    data[3] = 0x10; assert(music_stream_receive(data,20,MUSIC_SOURCE_BLE));
    assert(!music_stream_read(&frame)); // Effect bits alone must not keep music active.
    data[3] = 3; assert(music_stream_receive(data,20,MUSIC_SOURCE_USB));
    assert(music_stream_read(&frame) && frame.effect == MUSIC_EFFECT_SPECTRUM);
}
''')
        self.run_native(['platforms/nrf5/music_stream.c'])

    def test_custom_rgb_is_scoped_to_transport_and_does_not_extend_audio(self):
        self.write('test.c', r'''
#include <assert.h>
#include "music_stream.h"
unsigned critical_depth;
static uint32_t now;
uint32_t timer_read32(void) { return now; }
uint32_t timer_elapsed32(uint32_t at) { return now-at; }
int main(void) {
    music_frame_t frame;
    uint8_t color[20]={0xC7,2,255,128,64};
    uint8_t audio[20]={0xC7,1,0,MUSIC_MATRIX|MUSIC_MONOCHROME};
    assert(music_stream_receive(color,20,MUSIC_SOURCE_USB));
    assert(!music_stream_read(&frame));
    music_stream_receive(audio,20,MUSIC_SOURCE_USB);
    assert(music_stream_read(&frame) && frame.custom_color && frame.color.r==255 && frame.color.g==128 && frame.color.b==64);
    color[2]=9; color[3]=3; color[4]=7;
    music_stream_receive(color,20,MUSIC_SOURCE_BLE);
    assert(music_stream_read(&frame) && frame.color.r==255); // BLE color cannot recolor USB music.
    music_stream_receive(audio,20,MUSIC_SOURCE_BLE);
    assert(music_stream_read(&frame) && frame.color.r==9 && frame.color.g==3);
    color[5]=1; assert(!music_stream_receive(color,20,MUSIC_SOURCE_BLE));
    assert(music_stream_read(&frame) && frame.color.r==9);
    color[5]=0; now=749; music_stream_receive(color,20,MUSIC_SOURCE_BLE);
    now=750; assert(!music_stream_read(&frame)); // Color writes do not refresh the music timeout.
    music_stream_receive(audio,20,MUSIC_SOURCE_BLE);
    audio[3]=0; music_stream_receive(audio,20,MUSIC_SOURCE_BLE);
    audio[3]=MUSIC_MATRIX|MUSIC_MONOCHROME;
    music_stream_receive(audio,20,MUSIC_SOURCE_BLE);
    assert(music_stream_read(&frame) && !frame.custom_color); // Stop clears the per-stream override.
    color[2]=color[3]=color[4]=0; music_stream_receive(color,20,MUSIC_SOURCE_BLE);
    assert(music_stream_read(&frame) && frame.custom_color && !frame.color.r && !frame.color.g && !frame.color.b);
    music_stream_disconnect(MUSIC_SOURCE_BLE); music_stream_receive(audio,20,MUSIC_SOURCE_BLE);
    assert(music_stream_read(&frame) && !frame.custom_color);
    music_stream_disconnect(0); music_stream_disconnect(7); // Invalid sources are harmless.
}
''')
        self.run_native(['platforms/nrf5/music_stream.c'])

    def renderer_prefix(self, led_count=4):
        self.write('quantum.h', '''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "timer.h"
#define RGB_MATRIX_ENABLE
#define RGBLIGHT_ENABLE
#define NO_LED 255
#define MATRIX_ROWS 5
#define MATRIX_COLS 14
#define RGB_MATRIX_LED_COUNT 4
#define RGBLIGHT_LED_COUNT 18
#define RGB_MATRIX_CUSTOM_MUSIC_SPECTRUM 42
#define RGB_MATRIX_CUSTOM_MUSIC_DUAL_DANCE 43
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef struct {} keyrecord_t;
typedef struct { uint8_t h,s,v; } hsv_t;
typedef struct { uint8_t r,g,b; } rgb_t;
typedef struct { uint8_t matrix_co[MATRIX_ROWS][MATRIX_COLS]; struct { uint8_t x,y; } point[RGB_MATRIX_LED_COUNT]; uint8_t flags[RGB_MATRIX_LED_COUNT]; } led_config_t;
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
uint8_t rgblight_get_hue(void);
uint8_t rgblight_get_sat(void);
void rgblight_timer_disable(void);
void rgblight_set(void);
uint16_t timer_read(void);
uint16_t timer_elapsed(uint16_t);
'''.replace('RGB_MATRIX_LED_COUNT 4', f'RGB_MATRIX_LED_COUNT {led_count}'))
        return r'''
#include <assert.h>
#include <string.h>
#include "nrf5_lighting.h"
#include "nrf5_music.h"
#include "music_stream.h"
#include "rgblight_drivers.h"
unsigned critical_depth;
static uint32_t now;
static bool matrix = true, under = true, selftest;
static uint8_t under_brightness = 100;
static unsigned restores, flushes, timer_disables;
static uint8_t matrix_value[RGB_MATRIX_LED_COUNT], matrix_sat[RGB_MATRIX_LED_COUNT], matrix_hue[RGB_MATRIX_LED_COUNT], under_value[18], under_hue[18], under_sat[18];
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
void rgb_matrix_set_color(int i,uint8_t r,uint8_t g,uint8_t b) { assert(i>=0 && i<RGB_MATRIX_LED_COUNT); matrix_value[i]=b; matrix_sat[i]=g; matrix_hue[i]=r; }
bool rgblight_is_enabled(void) { return under; }
void rgblight_mode_noeeprom(uint8_t mode) { assert(mode==7); restores++; }
uint8_t rgblight_get_mode(void) { return 7; }
uint8_t rgblight_get_val(void) { return under_brightness; }
uint8_t rgblight_get_hue(void) { return 23; }
uint8_t rgblight_get_sat(void) { return 180; }
void rgblight_timer_disable(void) { timer_disables++; }
void rgblight_set(void) { flushes++; }
bool nrf5_underglow_test_is_active(void) { return selftest; }
static void color(int i,uint8_t r,uint8_t g,uint8_t b) { assert(i>=0 && i<18); under_value[i]=b; under_hue[i]=r; under_sat[i]=g; }
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
static void draw_dance(bool init) {
    effect_params_t params = {.init=init};
    assert(MUSIC_DUAL_DANCE(&params)); params.iter++;
    assert(!MUSIC_DUAL_DANCE(&params));
}

'''

    def test_renderer_independent_off_selftest_and_restore(self):
        self.write('test.c', self.renderer_prefix() + r'''
int main(void) {
    memset(g_led_config.matrix_co, 255, sizeof(g_led_config.matrix_co));
    g_led_config.matrix_co[0][0]=0; g_led_config.matrix_co[4][13]=1;
    g_led_config.matrix_co[2][7]=2; g_led_config.matrix_co[4][0]=3;
    uint8_t data[20]={0xC7,1,0,3};
    memset(data+4,255,16);
    assert(rgb_matrix_override_effect(7)==7);
    nrf5_music_task(); assert(flushes==0 && restores==0 && timer_disables==0);
    music_stream_receive(data,20,MUSIC_SOURCE_USB);
    assert(rgb_matrix_override_effect(7)==RGB_MATRIX_CUSTOM_MUSIC_SPECTRUM);
    assert(rgb_matrix_override_effect(0)==0); // Off must never be overridden.
    draw(true);
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==128); // Immediate attack.
    for (unsigned step=0; step<60; ++step) {
        now+=20; music_stream_receive(data,20,MUSIC_SOURCE_USB); draw(false);
    }
    nrf5_music_task();
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==128);
    for (unsigned i=0;i<4;i++) assert(matrix_sat[i]==255); // No white peak speckles.
    assert(matrix_hue[0]==0 && matrix_hue[1]==85 && matrix_hue[3]==85); // Red top, green base.
    for (unsigned i=0;i<18;i++) assert(under_value[i]==100);
    assert(flushes==1);
    nrf5_music_task(); assert(flushes==1);
    under=false; nrf5_music_task(); assert(flushes==2 && restores==0);
    under=true; now+=33; nrf5_music_task(); assert(flushes==3);
    selftest=true; now+=33; nrf5_music_task(); assert(flushes==3);
    selftest=false;
    data[3]=MUSIC_MATRIX;
    music_stream_receive(data,20,MUSIC_SOURCE_USB); nrf5_music_task();
    assert(restores==1); nrf5_music_task(); assert(restores==1);
    matrix=false; memset(matrix_value,17,4); draw(false);
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==17);
    matrix=true;
    // Use a single snapshot for both render batches, even if a packet arrives between them.
    effect_params_t params={.init=true};
    MUSIC_SPECTRUM(&params);
    memset(data+4,0,16); music_stream_receive(data,20,MUSIC_SOURCE_USB);
    params.iter=1; MUSIC_SPECTRUM(&params);
    assert(matrix_value[1]>0 && matrix_value[1]==matrix_value[3]);
    now+=100; draw(false);
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==0); // Short release, no detached peak.
    for (unsigned i=0;i<120;i++) { now+=20; draw(false); }
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==0);
    assert(rgb_matrix_override_effect(7)==7); // Timeout returns the configured mode.
    g_led_config.flags[3]=2; matrix_value[3]=17;
    draw(false); assert(matrix_value[3]==17); // Standard LED flags are respected.
    data[3]=3; music_stream_receive(data,20,MUSIC_SOURCE_USB); nrf5_music_task();
    now+=750; nrf5_music_task(); assert(restores==2);
    nrf5_music_task(); assert(restores==2);
    // Filled dual-center circles rise smoothly, survive timer wrap and never
    // drop out or retrigger during a steady note. Equal radii on both halves.
    g_led_config = (led_config_t){.point={{56,32},{80,32},{168,32},{192,32}}, .flags={1,1,1,1}};
    now = UINT32_MAX - 60;
    data[3] = 0x13; memset(data+4,255,16);
    music_stream_receive(data,20,MUSIC_SOURCE_USB);
    assert(rgb_matrix_override_effect(7)==RGB_MATRIX_CUSTOM_MUSIC_DUAL_DANCE);
    draw_dance(true);
    assert(matrix_value[0]>0 && matrix_value[2]==matrix_value[0]);
    assert(matrix_value[1]==0 && matrix_value[3]==0); // The initial field grows from its center.
    uint8_t previous = matrix_value[0];
    for (unsigned step=0; step<120; ++step) {
        now+=20; data[2]++; music_stream_receive(data,20,MUSIC_SOURCE_USB);
        draw_dance(false);
        assert(matrix_value[0]>=previous && matrix_value[0]==matrix_value[2]);
        assert(matrix_value[1]==matrix_value[3]);
        previous=matrix_value[0];
    }
    assert(matrix_value[0]==128 && matrix_value[1]==128);
    uint8_t old_hue=matrix_hue[0];
    data[2]+=123; music_stream_receive(data,20,MUSIC_SOURCE_USB); draw_dance(false);
    assert(matrix_hue[0]==old_hue); // Color follows time, never transport packet sequence.
    // A quiet packet holds the envelope rather than immediately blacking out.
    memset(data+4,0,16); music_stream_receive(data,20,MUSIC_SOURCE_USB);
    now+=40; draw_dance(false); assert(matrix_value[0]>0 && matrix_value[1]==0);
    for (unsigned step=0; step<200; ++step) {
        now+=20; music_stream_receive(data,20,MUSIC_SOURCE_USB); draw_dance(false);
        if (step==5) {
            assert(matrix_value[1]==0 && matrix_value[3]==0); // Outer rings contract within 160 ms.
            assert(matrix_value[0]<64 && matrix_value[2]==matrix_value[0]);
        }
    }
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==0);
    matrix=false; memset(matrix_value,17,4); draw_dance(true);
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==17);
    matrix=true;
    memset(data+4,255,16); music_stream_receive(data,20,MUSIC_SOURCE_BLE);
    draw_dance(true); assert(matrix_value[0]>0);
    g_led_config.flags[3]=2; matrix_value[3]=17; now+=20;
    draw_dance(false); assert(matrix_value[3]==17);
    // Changing modes starts from silence and fades in instead of flashing.
    memset(data+4,0,16); music_stream_receive(data,20,MUSIC_SOURCE_BLE);
    g_led_config.flags[3]=1; draw_dance(true);
    for (unsigned i=0;i<4;i++) assert(matrix_value[i]==0);
}
''')
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

    def test_spectrum_aligns_all_staggered_keys_and_frequency_bands(self):
        board = (ROOT / 'keyboards/ciank67/ciank67.c').read_text()
        layout = board.split('led_config_t g_led_config = ', 1)[1].split(';', 1)[0]
        self.write('test.c', self.renderer_prefix(67) + '\nint main(void) {\n'
                   + 'g_led_config = (led_config_t)' + layout + ';\n' + r'''
    // Exercise every input band against the actual 67-key LED/matrix layout.
    for (uint8_t band=0; band<MUSIC_BANDS; ++band) {
        uint8_t data[20]={0xC7,1,0,MUSIC_MATRIX}; data[4+band]=255;
        uint8_t expected=0;
        for (uint8_t bar=0; bar<7; ++bar)
            if (band>=bar*16/7 && band<(bar+1)*16/7) expected=bar;
        for (uint8_t i=0;i<67;i++) g_led_config.flags[i]=1;
        for (unsigned step=0; step<70; ++step) {
            now+=20; music_stream_receive(data,20,MUSIC_SOURCE_USB);
            nrf5_music_render_spectrum(0,30,step==0);
            nrf5_music_render_spectrum(30,67,step==0);
        }
        for (unsigned row=0; row<MATRIX_ROWS; ++row) {
            for (unsigned col=0; col<MATRIX_COLS; ++col) {
                uint8_t led=g_led_config.matrix_co[row][col];
                if (led==NO_LED) continue;
                assert(matrix_value[led]==(col/2==expected ? 128 : 0));
                assert(matrix_sat[led]==255);
                assert(matrix_hue[led]==row*85/4);
            }
        }
    }
    // Mid-height bars must have no floating dots, and staggered keys at the
    // same row/group must have exactly the same brightness.
    uint8_t data[20]={0xC7,1,0,MUSIC_MATRIX}; data[9]=128;
    for (unsigned step=0; step<70; ++step) {
        now+=20; music_stream_receive(data,20,MUSIC_SOURCE_BLE);
        nrf5_music_render_spectrum(0,67,step==0);
    }
    uint8_t above=0;
    for (uint8_t row=0; row<MATRIX_ROWS; ++row) {
        uint8_t row_value=0;
        bool found=false;
        for (uint8_t col=4; col<6; ++col) {
            uint8_t led=g_led_config.matrix_co[row][col];
            if (led==NO_LED) continue;
            if (found) assert(matrix_value[led]==row_value);
            row_value=matrix_value[led]; found=true;
        }
        assert(found && row_value>=above); above=row_value;
        if (row==0) assert(row_value==0);
        if (row<4) assert(row_value==0); // Moderate input no longer fills two rows.
    }
    assert(above>0 && above<64); // Base brightness follows the audio as well.
}
''')
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

    def test_shared_renderer_supports_a_4_by_12_keyboard(self):
        prefix = self.renderer_prefix(48)
        quantum = (self.work / 'quantum.h').read_text().replace('MATRIX_ROWS 5', 'MATRIX_ROWS 4').replace('MATRIX_COLS 14', 'MATRIX_COLS 12')
        self.write('quantum.h', quantum)
        self.write('test.c', prefix + r"""
int main(void) {
    for (unsigned row=0;row<4;row++) for(unsigned col=0;col<12;col++) {
        unsigned led=row*12+col;
        g_led_config.matrix_co[row][col]=led;
        g_led_config.point[led].x=col*224/11;
        g_led_config.point[led].y=row*64/3;
        g_led_config.flags[led]=1;
    }
    uint8_t packet[20]={0xC7,1,0,MUSIC_MATRIX};memset(packet+4,255,16);
    assert(music_stream_receive(packet,20,MUSIC_SOURCE_USB));
    nrf5_music_render_spectrum(0,48,true);
    for(unsigned i=0;i<48;i++)assert(matrix_value[i]>0);
    memset(packet+4,0,16);assert(music_stream_receive(packet,20,MUSIC_SOURCE_USB));now+=150;
    nrf5_music_render_spectrum(0,48,false);
    for(unsigned i=0;i<48;i++)assert(matrix_value[i]==0);
    return 0;
}
""")
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

    def test_spectrum_attacks_immediately_and_releases_within_120ms(self):
        prefix = self.renderer_prefix().replace(
            'uint8_t rgb_matrix_get_speed(void) { return 128; }',
            'static uint8_t speed; uint8_t rgb_matrix_get_speed(void) { return speed; }')
        self.write('test.c', prefix + r'''
int main(void) {
    memset(g_led_config.matrix_co,255,sizeof(g_led_config.matrix_co));
    g_led_config.matrix_co[0][0]=0; g_led_config.matrix_co[4][13]=1;
    g_led_config.matrix_co[2][7]=2; g_led_config.matrix_co[4][0]=3;
    const uint8_t speeds[]={0,128,255};
    for (unsigned n=0;n<3;n++) {
        speed=speeds[n]; now=UINT32_MAX-30;
        uint8_t data[20]={0xC7,1,0,MUSIC_MATRIX}; memset(data+4,255,16);
        music_stream_receive(data,20,MUSIC_SOURCE_USB); draw(true);
        for (unsigned i=0;i<4;i++) assert(matrix_value[i]==128);
        memset(data+4,0,16); music_stream_receive(data,20,MUSIC_SOURCE_USB);
        uint8_t previous[4]; memcpy(previous,matrix_value,4);
        for (unsigned elapsed=10;elapsed<=120;elapsed+=10) {
            now+=10; draw(false);
            for (unsigned i=0;i<4;i++) {
                assert(matrix_value[i]<=previous[i]); previous[i]=matrix_value[i];
            }
            if (elapsed==30) assert(matrix_value[0]==0); // Peak cannot hang at the top.
        }
        for (unsigned i=0;i<4;i++) assert(matrix_value[i]==0);
    }
}
''')
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

    def test_moderate_levels_do_not_pin_the_base_or_dance_centers(self):
        self.write('test.c', self.renderer_prefix() + r'''
int main(void) {
    uint8_t data[20]={0xC7,1,0,MUSIC_MATRIX};
    memset(g_led_config.matrix_co,255,sizeof(g_led_config.matrix_co));
    g_led_config.matrix_co[4][0]=0; g_led_config.matrix_co[3][0]=1;
    g_led_config.matrix_co[2][0]=2; g_led_config.matrix_co[0][0]=3;
    g_led_config.point[0]=(typeof(g_led_config.point[0])){0,64};
    g_led_config.point[1]=(typeof(g_led_config.point[0])){0,48};
    g_led_config.point[2]=(typeof(g_led_config.point[0])){0,32};
    g_led_config.point[3]=(typeof(g_led_config.point[0])){0,0};
    memset(data+4,128,16); music_stream_receive(data,20,MUSIC_SOURCE_USB); draw(true);
    uint8_t moderate_base=matrix_value[0];
    assert(moderate_base>0 && moderate_base<64);
    assert(matrix_value[1]==0 && matrix_value[2]==0 && matrix_value[3]==0);
    memset(data+4,192,16); music_stream_receive(data,20,MUSIC_SOURCE_USB); draw(false);
    assert(matrix_value[0]>moderate_base && matrix_value[0]<128);
    assert(matrix_value[1]>0 && matrix_value[2]>0 && matrix_value[3]==0);
    g_led_config = (led_config_t){.point={{60,32},{80,32},{172,32},{192,32}}, .flags={1,1,1,1}};
    data[3]=0x11;
    const uint8_t inputs[]={64,96,128,160,192,224,255};
    uint8_t previous=0;
    for (unsigned n=0;n<sizeof(inputs);n++) {
        memset(data+4,inputs[n],16);
        for (unsigned step=0;step<70;step++) {
            now+=20; music_stream_receive(data,20,MUSIC_SOURCE_USB); draw_dance(step==0);
        }
        assert(matrix_value[0]==matrix_value[2] && matrix_value[1]==matrix_value[3]);
        assert(matrix_value[0]>=previous);
        if (inputs[n]==128) assert(matrix_value[0]>0 && matrix_value[0]<64 && matrix_value[1]==0);
        if (inputs[n]>128) assert(matrix_value[0]>previous);
        if (inputs[n]<255) assert(matrix_value[0]<128);
        previous=matrix_value[0];
    }
    assert(matrix_value[0]==128);
}
''')
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

    def test_color_mode_changes_hue_without_changing_music_motion(self):
        self.write('test.c', self.renderer_prefix() + r'''
int main(void) {
    memset(g_led_config.matrix_co,255,sizeof(g_led_config.matrix_co));
    g_led_config.matrix_co[0][0]=0; g_led_config.matrix_co[4][13]=1;
    g_led_config.matrix_co[2][7]=2; g_led_config.matrix_co[4][0]=3;
    uint8_t data[20]={0xC7,1,0,3}; memset(data+4,192,16);
    music_stream_receive(data,20,MUSIC_SOURCE_USB); draw(true); nrf5_music_task();
    assert(matrix_hue[0]!=matrix_hue[1] && under_hue[0]!=under_hue[17]);
    uint8_t spectrum_values[4]; memcpy(spectrum_values,matrix_value,4);
    assert(spectrum_values[0]==0 && spectrum_values[1]>0 && spectrum_values[1]<128);
    data[3]=0x23; now+=33; music_stream_receive(data,20,MUSIC_SOURCE_BLE);
    draw(false); nrf5_music_task();
    for (unsigned i=0;i<4;i++) assert(matrix_hue[i]==0 && matrix_value[i]==spectrum_values[i]);
    for (unsigned i=0;i<18;i++) assert(under_hue[i]==23 && under_sat[i]==180 && under_value[i]==75);
    // The color selection, like the heights, is held for the entire frame.
    effect_params_t params={0}; MUSIC_SPECTRUM(&params);
    data[3]=3; music_stream_receive(data,20,MUSIC_SOURCE_USB);
    params.iter=1; MUSIC_SPECTRUM(&params);
    for (unsigned i=0;i<4;i++) assert(matrix_hue[i]==0);
    now+=33; draw(false); nrf5_music_task();
    assert(matrix_hue[0]!=matrix_hue[1] && under_hue[0]!=under_hue[17]);
    g_led_config = (led_config_t){.point={{56,32},{80,32},{168,32},{192,32}}, .flags={1,1,1,1}};
    data[3]=0x13;
    for (unsigned step=0;step<70;step++) {
        now+=20; music_stream_receive(data,20,MUSIC_SOURCE_USB); draw_dance(step==0);
    }
    uint8_t values[4]; memcpy(values,matrix_value,4);
    assert(matrix_hue[0]!=matrix_hue[1]);
    data[3]=0x33; music_stream_receive(data,20,MUSIC_SOURCE_BLE); draw_dance(false);
    for (unsigned i=0;i<4;i++) assert(matrix_hue[i]==0 && matrix_value[i]==values[i]);
    data[3]=0x13; music_stream_receive(data,20,MUSIC_SOURCE_USB); draw_dance(false);
    assert(matrix_hue[0]!=matrix_hue[1] && !memcmp(values,matrix_value,4));
}
''')
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

    def test_zero_saved_underglow_brightness_is_visible_only_during_music(self):
        self.write('test.c', self.renderer_prefix() + r"""
int main(void) {
    under_brightness=0;
    uint8_t packet[20]={0xC7,1,9,2};memset(packet+4,255,16);
    assert(music_stream_receive(packet,20,MUSIC_SOURCE_ESB));nrf5_music_task();
    assert(flushes==1&&under_brightness==0);
    for(unsigned i=0;i<18;i++)assert(under_value[i]==128);
    under=false;now+=40;nrf5_music_task();assert(flushes==2&&under_brightness==0);
    under=true;now+=40;nrf5_music_task();assert(flushes==3);
    packet[3]=0;assert(music_stream_receive(packet,20,MUSIC_SOURCE_ESB));nrf5_music_task();
    assert(restores==1&&under_brightness==0);
}
""")
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

    def test_custom_rgb_recolors_both_effects_and_underglow(self):
        self.write('test.c', self.renderer_prefix() + r'''
int main(void) {
    memset(g_led_config.matrix_co,255,sizeof(g_led_config.matrix_co));
    g_led_config.matrix_co[0][0]=0; g_led_config.matrix_co[4][13]=1;
    g_led_config.matrix_co[2][7]=2; g_led_config.matrix_co[4][0]=3;
    uint8_t audio[20]={0xC7,1,0,0x23}; memset(audio+4,255,16);
    uint8_t color[20]={0xC7,2,255,128,64};
    music_stream_receive(color,20,MUSIC_SOURCE_USB);
    music_stream_receive(audio,20,MUSIC_SOURCE_USB);
    draw(true); nrf5_music_task();
    for (unsigned i=0;i<4;i++) assert(matrix_hue[i]==128 && matrix_sat[i]==64 && matrix_value[i]==32);
    for (unsigned i=0;i<18;i++) assert(under_hue[i]==100 && under_sat[i]==50 && under_value[i]==25);
    // A newly selected blue is held until the next complete matrix frame.
    effect_params_t params={0}; MUSIC_SPECTRUM(&params);
    color[2]=color[3]=0; color[4]=255; music_stream_receive(color,20,MUSIC_SOURCE_USB);
    params.iter=1; MUSIC_SPECTRUM(&params);
    assert(matrix_hue[3]==128 && matrix_value[3]==32);
    draw(false);
    for (unsigned i=0;i<4;i++) assert(matrix_hue[i]==0 && matrix_sat[i]==0 && matrix_value[i]==128);
    audio[3]=0x33;
    for (unsigned step=0;step<70;step++) {
        now+=20; music_stream_receive(audio,20,MUSIC_SOURCE_USB); draw_dance(step==0);
    }
    for (unsigned i=0;i<4;i++) assert(matrix_hue[i]==0 && matrix_sat[i]==0 && matrix_value[i]>0);
    color[4]=0; music_stream_receive(color,20,MUSIC_SOURCE_USB); draw_dance(false); nrf5_music_task();
    for (unsigned i=0;i<4;i++) assert(!matrix_hue[i] && !matrix_sat[i] && !matrix_value[i]);
    for (unsigned i=0;i<18;i++) assert(!under_hue[i] && !under_sat[i] && !under_value[i]);
    // Selected RGB never affects colorful mode.
    audio[3]=3; music_stream_receive(audio,20,MUSIC_SOURCE_USB); draw(false);
    assert(matrix_hue[0]!=matrix_hue[1] && matrix_value[3]>0);
}
''')
        self.run_native(['platforms/nrf5/music_stream.c', 'platforms/nrf5/features/music.c'])

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
static bool suspend_state, streaming, platform_suspended;
static bool rgb_matrix_platform_suspended(void) { return platform_suspended; }
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
    rgb_matrix_config.mode = 7; platform_suspended = true; rgb_task_start();
    assert(rgb_current_effect == 0 && calls == 3 && rgb_matrix_config.enable);
}
''')
        self.run_native([])

    def test_actual_usb_output_callback(self):
        code = (ROOT / 'tmk_core/protocol/nrf5/usb/nrf5_usb.c').read_text()
        callback = code.split('static void music_usb_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event) {', 1)[1].split('\n}\n#endif', 1)[0] + '\n}\n'
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
    report[4]=0x33; music_usb_event(&instance,1);
    assert(music_stream_read(&frame) && frame.flags==3 && frame.effect==MUSIC_EFFECT_DUAL_DANCE && frame.monochrome);
    report[2]=2; report[3]=255; report[4]=128; report[5]=64;
    music_usb_event(&instance,1);
    assert(music_stream_read(&frame) && frame.custom_color && frame.color.r==255 && frame.color.g==128 && frame.color.b==64);
    report[2]=1;
    report[4]=0; music_usb_event(&instance,1); assert(!music_stream_read(&frame));
}
''')
        self.run_native(['platforms/nrf5/music_stream.c'])

    def test_actual_sdk_set_report_control_path_delivers_music_and_rgb(self):
        sdk = (ROOT / 'lib/nrf5/components/libraries/usbd/class/hid/generic/app_usbd_hid_generic.c').read_text()
        def function(name):
            return 'static ret_code_t ' + name + '(' + sdk.split('static ret_code_t ' + name + '(', 1)[1].split('\n}', 1)[0] + '\n}\n'
        self.write('test.c', r'''
#include <assert.h>
#include <string.h>
#include "app_util_platform.h"
#include "music_stream.h"
unsigned critical_depth;
uint32_t timer_read32(void) { return 0; }
uint32_t timer_elapsed32(uint32_t at) { return 0; }
typedef int ret_code_t;
typedef int nrf_drv_usbd_ep_status_t;
typedef struct { int unused; } app_usbd_class_inst_t;
typedef void (*app_usbd_hid_user_ev_handler_t)(const app_usbd_class_inst_t *, int);
typedef struct { struct { struct { app_usbd_hid_user_ev_handler_t user_event_handler; } hid_inst; } inst; } specific_t;
typedef struct { specific_t specific; } app_usbd_hid_generic_t;
typedef struct { struct { struct { uint8_t lb; } wValue; } setup; } app_usbd_setup_evt_t;
typedef struct { uint8_t *p_buff; size_t size; } app_usbd_hid_report_buffer_t;
typedef struct { ret_code_t (*handler)(nrf_drv_usbd_ep_status_t, void *); void *p_context; } app_usbd_core_setup_data_handler_desc_t;
#define NRF_USBD_EP_OK 0
#define NRF_ERROR_INTERNAL 1
#define NRF_SUCCESS 0
#define NRF_DRV_USBD_EPOUT0 0
#define APP_USBD_HID_USER_EVT_OUT_REPORT_READY 1
#define NRF_DRV_USBD_TRANSFER_OUT(name,buffer,length) struct { uint8_t *data; size_t size; } name={buffer,length}
static uint8_t report[21], incoming[20];
static app_usbd_hid_report_buffer_t output={report,21};
static app_usbd_hid_generic_t generic;
static app_usbd_core_setup_data_handler_desc_t callback;
static const app_usbd_hid_generic_t *hid_generic_get(const app_usbd_class_inst_t *instance) { return &generic; }
static const app_usbd_hid_report_buffer_t *app_usbd_hid_rep_buff_out_get(const void *instance) { return &output; }
static ret_code_t app_usbd_ep_transfer(int ep, const void *ptr) {
    const struct { uint8_t *data; size_t size; } *transfer=ptr;
    assert(ep==0 && transfer->data==report+1 && transfer->size==20 && critical_depth==1);
    memcpy(transfer->data,incoming,20); return 0;
}
static ret_code_t app_usbd_core_setup_data_handler_set(int ep, const app_usbd_core_setup_data_handler_desc_t *value) {
    callback=*value; return 0;
}
static void delivered(const app_usbd_class_inst_t *instance, int event) {
    assert(event==APP_USBD_HID_USER_EVT_OUT_REPORT_READY && report[0]==4);
    assert(music_stream_receive(report+1,20,MUSIC_SOURCE_USB));
}
''' + function('hid_generic_on_set_report_data_cb') + function('hid_generic_on_set_report') + r'''
int main(void) {
    generic.specific.inst.hid_inst.user_event_handler=delivered;
    app_usbd_setup_evt_t setup={.setup.wValue.lb=4};
    app_usbd_class_inst_t instance={0}; music_frame_t frame;
    incoming[0]=0xC7; incoming[1]=1; incoming[3]=0x23; incoming[4]=200;
    assert(hid_generic_on_set_report(&instance,&setup)==0);
    assert(callback.handler(0,callback.p_context)==0);
    assert(music_stream_read(&frame) && frame.bands[0]==200 && frame.monochrome);
    memset(incoming,0,20); incoming[0]=0xC7; incoming[1]=2; incoming[2]=255; incoming[3]=128; incoming[4]=64;
    assert(hid_generic_on_set_report(&instance,&setup)==0);
    assert(callback.handler(0,callback.p_context)==0);
    assert(music_stream_read(&frame) && frame.custom_color && frame.color.r==255 && frame.color.g==128 && frame.color.b==64);
    assert(callback.handler(1,callback.p_context)==NRF_ERROR_INTERNAL);
    memset(incoming,0,20); incoming[0]=0xC7; incoming[1]=1;
    assert(hid_generic_on_set_report(&instance,&setup)==0);
    assert(callback.handler(0,callback.p_context)==0 && !music_stream_read(&frame));
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
    write->data[3] = 0x33; write->op = BLE_GATTS_OP_WRITE_CMD;
    music_ble_event(&event, NULL);
    assert(music_stream_read(&frame) && frame.flags == 3 && frame.effect == MUSIC_EFFECT_DUAL_DANCE && frame.monochrome && activity == 2);
    write->data[1]=2; write->data[2]=9; write->data[3]=3; write->data[4]=7;
    music_ble_event(&event, NULL);
    assert(music_stream_read(&frame) && frame.custom_color && frame.color.r==9 && frame.color.g==3 && activity==2);
    write->data[1]=1;
    write->data[3] = 0x10;
    music_ble_event(&event, NULL); assert(!music_stream_read(&frame) && activity == 2);
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
