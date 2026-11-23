# SPDX-License-Identifier: GPL-2.0-or-later
"""Wireless standby, GPIO wake races, resource fallback and rollover checks."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class WirelessPowerTests(unittest.TestCase):
    def check_policy(self, direction, failure):
        if not shutil.which('cc'):
            self.skipTest('Native C compiler is required')
        headers = {
            'nrf.h': '''#pragma once
#include <stdint.h>
static struct {uint32_t USBREGSTATUS;} power;
static struct {uint32_t ENABLE;} usb;
#define NRF_POWER (&power)
#define NRF_USBD (&usb)
#define POWER_USBREGSTATUS_VBUSDETECT_Msk 1
''',
            'keyboard.h': '#include <stdint.h>\nuint32_t last_input_activity_time(void);\n',
            'matrix.h': '#include <stdint.h>\nuint16_t matrix_get_row(uint8_t);\n',
            'timer.h': '#include <stdint.h>\nuint32_t timer_read32(void);\n',
            'app_util_platform.h': '#define CRITICAL_REGION_ENTER() {\n#define CRITICAL_REGION_EXIT() }\n',
            'rgblight.h': '#include <stdbool.h>\n#include <stdint.h>\nbool rgblight_is_enabled(void);\nuint8_t rgblight_get_mode(void);\nvoid rgblight_mode_noeeprom(uint8_t);\nvoid rgblight_set(void);\n',
            'audio.h': '#include <stdbool.h>\nbool audio_is_playing_note(void);\nbool audio_is_playing_melody(void);\n',
            'nrfx_gpiote.h': '''#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef unsigned nrfx_gpiote_pin_t;
typedef unsigned nrf_gpiote_polarity_t;
typedef struct {bool hi_accuracy; unsigned pull;} nrfx_gpiote_in_config_t;
#define NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(high) {.hi_accuracy=high}
#define NRFX_SUCCESS 0
#define NRF_GPIOTE_POLARITY_HITOLO 1
bool nrfx_gpiote_is_init(void);
int nrfx_gpiote_init(void);
int nrfx_gpiote_in_init(unsigned, const nrfx_gpiote_in_config_t *, void (*)(unsigned,unsigned));
void nrfx_gpiote_in_uninit(unsigned);
void nrfx_gpiote_in_event_disable(unsigned);
void nrfx_gpiote_in_event_enable(unsigned,bool);
''',
            'nrf_gpio.h': '#define NRF_GPIO_PIN_PULLUP 1\nvoid nrf_gpio_pin_clear(unsigned);\nvoid nrf_gpio_cfg_output(unsigned);\nunsigned nrf_gpio_pin_read(unsigned);\n',
        }
        source = r'''
#include <assert.h>
#include <stdlib.h>
#include "SOURCE"
static uint32_t clock_ticks, activity;
static bool flash, sound, tune, stream, pairing, settings;
static bool gpio_init, senses[48], registered[48], low[48];
static bool fail_gpio;
static unsigned initializations, restores, queued, drive_mask, freed, blanks;
static bool base_enabled=true;
static unsigned mode=3, mode_restores;
static void (*handler)(unsigned,unsigned);
uint32_t timer_read32(void) {return clock_ticks;}
uint32_t last_input_activity_time(void) {return activity;}
static uint16_t rows[2];
uint16_t matrix_get_row(uint8_t row) {return rows[row];}
bool nrf5_eeprom_pending(void) {return flash;}
bool audio_is_playing_note(void) {return sound;}
bool audio_is_playing_melody(void) {return tune;}
bool music_stream_read(music_frame_t *f) {return stream;}
bool nrf5_pair_busy(void) {return pairing;}
bool nrf5_esb_settings_pending(void) {return settings;}
bool rgblight_is_enabled(void) {return base_enabled;}
uint8_t rgblight_get_mode(void) {return mode;}
void rgblight_mode_noeeprom(uint8_t value) {assert(value==mode && !nrf5_wireless_idle()); mode_restores++;}
void rgblight_set(void) {blanks++;}
bool nrfx_gpiote_is_init(void) {return gpio_init;}
int nrfx_gpiote_init(void) {gpio_init=true; return 0;}
int nrfx_gpiote_in_init(unsigned pin,const nrfx_gpiote_in_config_t *config,void (*callback)(unsigned,unsigned)) {
    assert(!config->hi_accuracy && config->pull==1 && !registered[pin]);
    if(fail_gpio && initializations==1) return -1;
    initializations++;registered[pin]=true;handler=callback;return 0;
}
void nrfx_gpiote_in_uninit(unsigned pin) {assert(registered[pin]);registered[pin]=false;freed++;}
void nrfx_gpiote_in_event_disable(unsigned pin) {assert(registered[pin]);senses[pin]=false;}
void nrfx_gpiote_in_event_enable(unsigned pin,bool enable) {assert(registered[pin] && enable);senses[pin]=true;}
void nrf_gpio_pin_clear(unsigned pin) {drive_mask|=1u<<pin;}
void nrf_gpio_cfg_output(unsigned pin) {assert(drive_mask & (1u<<pin));}
unsigned nrf_gpio_pin_read(unsigned pin) {return !low[pin];}
void matrix_init_pins(void) {restores++;drive_mask=0;}
void nrf5_keyboard_scan_wake(void) {queued++;}
static void advance(uint32_t ticks) {clock_ticks=(clock_ticks+ticks)&0x7ffff;nrf5_wireless_power_task();}
static void go_idle(void) {advance(31*1024);assert(nrf5_wireless_idle());}
static void scan(void) {nrf5_wireless_prepare_scan();assert(!drive_mask);nrf5_wireless_finish_scan();}
int main(int argc,char **argv) {
    fail_gpio=argc>1;
    clock_ticks=0x7ffff-1000;nrf5_wireless_power_task();
    advance(29*1024);assert(!nrf5_wireless_idle());
    advance(2*1024);assert(nrf5_wireless_idle()); // actual RTC rollover
    assert(base_enabled && mode==3 && blanks==1);
    scan();
    if(fail_gpio) {
        assert(nrf5_wireless_scan_interval()==6 && freed==1 && !drive_mask);
        for(unsigned i=0;i<48;i++)assert(!registered[i]&&!senses[i]);
        scan();assert(initializations==1); // fallback must not repeatedly allocate
        activity++;nrf5_wireless_power_task();assert(!nrf5_wireless_idle());return 0;
    }
    assert(nrf5_wireless_scan_interval()==100);
    for(unsigned i=0;i<sizeof(inputs);i++)assert(senses[inputs[i]]);
    for(unsigned i=0;i<sizeof(outputs);i++)assert(drive_mask&(1u<<outputs[i]));
    // IRQ must disable all level senses and schedule a real scan immediately.
    low[inputs[0]]=true;handler(inputs[0],1);
    assert(queued==1 && nrf5_wireless_scan_interval()==6);
    for(unsigned i=0;i<sizeof(inputs);i++)assert(!senses[inputs[i]]);
    nrf5_wireless_power_task();assert(!nrf5_wireless_idle() && mode_restores==1);
    nrf5_wireless_prepare_scan();assert(!drive_mask);
    low[inputs[0]]=false;
    // Held keys, including modifiers, prevent standby even with no new events.
    rows[0]=1;advance(40*1024);assert(!nrf5_wireless_idle());rows[0]=0;
    go_idle();activity++;nrf5_wireless_power_task();assert(!nrf5_wireless_idle());
    bool *blockers[]={&flash,&sound,&tune,&stream,&pairing,&settings};
    for(unsigned i=0;i<sizeof(blockers)/sizeof(*blockers);i++) {
        *blockers[i]=true;advance(40*1024);assert(!nrf5_wireless_idle());
        *blockers[i]=false;go_idle();activity++;nrf5_wireless_power_task();
    }
    // USB presence and an enabled USBD block standby independently.
    power.USBREGSTATUS=1;advance(40*1024);assert(!nrf5_wireless_idle());
    power.USBREGSTATUS=0;usb.ENABLE=1;advance(40*1024);assert(!nrf5_wireless_idle());usb.ENABLE=0;
    // Closing the arm/read race must request a scan, not strand a held key.
    go_idle();low[inputs[sizeof(inputs)-1]]=true;scan();assert(queued==2);
    nrf5_wireless_power_task();assert(!nrf5_wireless_idle());
    nrf5_wireless_prepare_scan();low[inputs[sizeof(inputs)-1]]=false;
    base_enabled=false;unsigned saved=mode_restores;
    go_idle();activity++;nrf5_wireless_power_task();assert(!base_enabled && mode_restores==saved);
    // Long idle must remain latched through another full short-RTC wrap.
    go_idle();for(unsigned i=0;i<600;i++)advance(1024);assert(nrf5_wireless_idle());
    nrf5_wireless_note_activity();nrf5_wireless_power_task();assert(!nrf5_wireless_idle());
    go_idle();scan();nrf5_wireless_before_system_off();assert(!drive_mask);
    for(unsigned i=0;i<48;i++)assert(!registered[i]&&!senses[i]);
}
'''.replace('SOURCE', str(ROOT/'platforms/nrf5/wireless_power.c'))
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            for name, text in headers.items():
                (work/name).write_text(text)
            (work/'test.c').write_text(source)
            binary = work/'test'
            flags = ['-DNRF5_MATRIX_IDLE_ENABLE', '-DMUSIC_STREAM_ENABLE', '-DAUDIO_ENABLE', '-DNRF5_ESB_ENABLE',
                     '-DRGBLIGHT_ENABLE', '-DMATRIX_ROWS=2', '-DMATRIX_COLS=3',
                     '-DMATRIX_ROW_PINS={1,2}', '-DMATRIX_COL_PINS={3,4,5}', '-DCOL2ROW=0', '-DROW2COL=1',
                     f'-DDIODE_DIRECTION={direction}', '-DKEYBOARD_SCAN_INTERVAL=6']
            command = ['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                       '-fsanitize=address,undefined','-fno-pie','-no-pie','-I',str(work),
                       '-I',str(ROOT/'platforms/nrf5'), *flags, str(work/'test.c'),'-o',str(binary)]
            for cmd in (command,[str(binary), *(['fail'] if failure else [])]):
                result=subprocess.run(cmd,capture_output=True,text=True,
                                      env={**os.environ,'ASAN_OPTIONS':'detect_stack_use_after_return=1'})
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_col2row_interrupt_wake_and_idle_policy(self):
        self.check_policy(0,False)

    def test_row2col_interrupt_wake_and_idle_policy(self):
        self.check_policy(1,False)

    def test_gpio_resource_failure_retains_fast_polling(self):
        self.check_policy(0,True)
