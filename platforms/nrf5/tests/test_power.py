# SPDX-License-Identifier: GPL-2.0-or-later
"""Battery sampling lifecycle and buffer recycling with the real ADC wrapper."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class PowerTests(unittest.TestCase):
    def test_battery_sampling_batch_preserves_sleep_timer_and_usb_reset(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('Native C compiler is required')
        service = (ROOT/'tmk_core/protocol/nrf5/ble/ble_service.c').read_text()
        start = service.index('static void battery_level_meas_timeout_handler(void *p_context) {')
        end = start + service[start:].index('{') + 1
        depth = 1
        while depth:
            depth += (service[end] == '{') - (service[end] == '}')
            end += 1
        function = service[start:end]
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work/'test.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define NRF5_WIRELESS_POWER_ENABLE
#define POWER_SAVE_TIMEOUT 2000
#define UNUSED_PARAMETER(x) (void)(x)
static bool powered, automatic_sleep_pending;
static uint16_t power_save_counter;
static unsigned samples,sleep_requests;
bool nrf5_usb_powered(void){return powered;}
void request_automatic_sleep(void){sleep_requests++;automatic_sleep_pending=true;}
void adc_start(void){samples++;}
''' + function + r'''
int main(void){
 for(unsigned i=0;i<4;i++)battery_level_meas_timeout_handler(NULL);
 assert(samples==4 && power_save_counter==4);
 for(unsigned i=0;i<26;i++)battery_level_meas_timeout_handler(NULL);
 assert(samples==4 && power_save_counter==30);
 for(unsigned i=0;i<60;i++)battery_level_meas_timeout_handler(NULL);
 assert(samples==12 && power_save_counter==90 && !sleep_requests);
 // USB suppresses measurement/sleep and re-arms a fresh four-sample batch.
 powered=true;automatic_sleep_pending=true;battery_level_meas_timeout_handler(NULL);
 assert(samples==12 && !power_save_counter && !automatic_sleep_pending);
 powered=false;for(unsigned i=0;i<4;i++)battery_level_meas_timeout_handler(NULL);
 assert(samples==16 && power_save_counter==4);
 power_save_counter=1001;battery_level_meas_timeout_handler(NULL);
 assert(sleep_requests==1 && automatic_sleep_pending);
}
''')
            binary = work/'test'
            for command in ([cc,'-std=gnu11','-Wall','-Wextra','-Werror',
                             '-fsanitize=address,undefined','-fno-pie','-no-pie',
                             str(work/'test.c'),'-o',str(binary)], [str(binary)]):
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_coalesced_maintenance_reuses_main_loop_and_preserves_watchdog_fallback(self):
        if not shutil.which('cc'):
            self.skipTest('Native C compiler is required')
        service = (ROOT/'tmk_core/protocol/nrf5/ble/ble_service.c').read_text()
        def function(signature):
            start = service.index(signature + ' {')
            end = service.index('{', start) + 1
            depth = 1
            while depth:
                depth += (service[end] == '{') - (service[end] == '}')
                end += 1
            return service[start:end]
        start = service.index('#if defined(NRF5_WIRELESS_POWER_ENABLE) && !defined(KBD_WDT_ENABLE)')
        declarations = service[start:service.index('#endif', start) + len('#endif')]
        functions = '\n'.join(function(name) for name in (
            'void nrf5_battery_task(void)', 'static void timers_init(void)', 'static void timers_start(void)'))
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define APP_TIMER_DEF(name) static unsigned name
#define APP_TIMER_MODE_REPEATED 1
#define BATTERY_LEVEL_MEAS_INTERVAL 2000
#define APP_ERROR_CHECK(x) assert((x)==0)
typedef int ret_code_t;
static unsigned creates,starts,samples;
static uint32_t now;
uint32_t timer_read32(void){return now & 0x7ffffu;}
uint32_t timer_elapsed32(uint32_t at){return (timer_read32()-at)&0x7ffffu;}
static void battery_level_meas_timeout_handler(void *context){(void)context;samples++;}
int app_timer_create(unsigned *id,unsigned kind,void (*cb)(void *)){
 assert(kind==1&&cb==battery_level_meas_timeout_handler);*id=1;creates++;return 0;
}
int app_timer_start(unsigned id,unsigned interval,void *context){assert(id==1&&interval==2000&&!context);starts++;return 0;}
''' + declarations + '\n' + functions + r'''
int main(void){
 now=0x7ffffu-1000;timers_init();timers_start();
#ifdef NRF5_BATTERY_COALESCED
 assert(!creates&&!starts);now+=2047;nrf5_battery_task();assert(!samples);
 now++;nrf5_battery_task();assert(samples==1);
 for(unsigned i=0;i<3600;i++){now+=1024;nrf5_battery_task();}
 assert(samples==1801); // an hour, crossing the RTC wrap repeatedly
 now+=20*1024;nrf5_battery_task();assert(samples==1802);
 nrf5_battery_task();assert(samples==1802); // no catch-up burst
#else
 assert(creates==1&&starts==1);now+=2048;nrf5_battery_task();assert(!samples);
 battery_level_meas_timeout_handler(NULL);assert(samples==1);
#endif
}
'''
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory);(work/'test.c').write_text(source)
            for flags in (['-DNRF5_WIRELESS_POWER_ENABLE'],
                          ['-DNRF5_WIRELESS_POWER_ENABLE','-DKBD_WDT_ENABLE'],
                          ['-DNRF5_WIRELESS_POWER_ENABLE','-DNRF5_WIRELESS_SYSTEM_OFF_ENABLE=1'], []):
                with self.subTest(flags=flags):
                    command=['cc','-std=gnu11','-fsanitize=address,undefined','-fno-pie','-no-pie',*flags,str(work/'test.c'),'-o',str(work/'test')]
                    for cmd in (command,[str(work/'test')]):
                        result=subprocess.run(cmd,capture_output=True,text=True)
                        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_low_power_sampling_keeps_battery_updates_and_recycles_both_buffers(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('Native C compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            for name in ('nrf_log.h', 'nrf_log_ctrl.h', 'nrfx_saadc.h'):
                (work / name).write_text('')
            (work / 'ble_service.h').write_text('''#include <stdint.h>
extern uint16_t voltage;
extern uint8_t battery_level;
void battery_level_update(int16_t, unsigned);
''')
            (work / 'nrf_drv_saadc.h').write_text(r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int ret_code_t;
typedef int16_t nrf_saadc_value_t;
typedef struct {bool low_power_mode;} nrf_drv_saadc_config_t;
typedef struct {unsigned input;} nrf_saadc_channel_config_t;
typedef struct {unsigned type; struct {struct {int16_t *p_buffer; unsigned size;} done;} data;} nrf_drv_saadc_evt_t;
#define NRF_DRV_SAADC_DEFAULT_CONFIG {false}
#define NRF_SAADC_INPUT_AIN3 3
#define NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(pin) {pin}
#define NRF_DRV_SAADC_EVT_DONE 1
#define APP_ERROR_CHECK(error) assert((error)==0)
int nrf_drv_saadc_init(const nrf_drv_saadc_config_t *, void (*)(const nrf_drv_saadc_evt_t *));
int nrfx_saadc_channel_init(unsigned, const nrf_saadc_channel_config_t *);
int nrfx_saadc_buffer_convert(int16_t *, unsigned);
int nrfx_saadc_sample(void);
''')
            (work / 'test.c').write_text(r'''
#include "nrf_drv_saadc.h"
#include "adc.h"
uint16_t voltage;
uint8_t battery_level;
static void (*callback)(const nrf_drv_saadc_evt_t *);
static int16_t *buffers[2];
static unsigned submitted, samples, updates;
static bool low_power;
int nrf_drv_saadc_init(const nrf_drv_saadc_config_t *config, void (*handler)(const nrf_drv_saadc_evt_t *)) {
    assert(config); low_power=config->low_power_mode; callback=handler; return 0;
}
int nrfx_saadc_channel_init(unsigned channel, const nrf_saadc_channel_config_t *config) {
    assert(channel==0 && config->input==3); return 0;
}
int nrfx_saadc_buffer_convert(int16_t *buffer, unsigned size) {
    assert(size==4);
    if(submitted<2) buffers[submitted]=buffer;
    else assert(buffer==buffers[(submitted-2)%2]);
    submitted++; return 0;
}
void battery_level_update(int16_t sum, unsigned count) {
    assert(sum==4000 && count==4); voltage=4000; battery_level=80; updates++;
}
int nrfx_saadc_sample(void) {
    // Model SDK low-power delivery of a full four-sample buffer; the wrapper
    // must recycle it without disrupting the alternate EasyDMA buffer.
    assert(low_power);
    unsigned slot=(samples/4)%2;
    buffers[slot][samples%4]=1000;
    if(++samples%4==0) {
        nrf_drv_saadc_evt_t done={.type=NRF_DRV_SAADC_EVT_DONE,
            .data.done={.p_buffer=buffers[slot],.size=4}};
        callback(&done);
    }
    return 0;
}
int main(void) {
    adc_init(); assert(low_power && submitted==2 && buffers[0]!=buffers[1]);
    for(unsigned i=0;i<24;i++) adc_start();
    assert(updates==6 && submitted==8 && get_vcc()==4000 && get_battery_level()==80);
}
''')
            binary = work / 'test'
            for command in ([cc, '-std=gnu11', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                             '-I', str(work), '-I', str(ROOT / 'tmk_core/protocol/nrf5'),
                             str(ROOT / 'tmk_core/protocol/nrf5/adc.c'), str(work / 'test.c'), '-o', str(binary)],
                            [str(binary)]):
                result = subprocess.run(command, capture_output=True, text=True,
                                        env={**os.environ, 'ASAN_OPTIONS': 'detect_stack_use_after_return=1'})
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
