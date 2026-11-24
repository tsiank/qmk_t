# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real dynamic BLE parameters without disconnect/reset side effects."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class BlePowerTests(unittest.TestCase):
    def run_case(self, case, dynamic=True):
        if not shutil.which('cc'):
            self.skipTest('Native C compiler is required')
        headers = {
            'ble.h': r'''#pragma once
#include <stdint.h>
#define BLE_CONN_HANDLE_INVALID 65535
#define BLE_GAP_EVT_CONNECTED 1
#define BLE_GAP_EVT_DISCONNECTED 2
#define BLE_GAP_EVT_CONN_PARAM_UPDATE 3
typedef struct {uint16_t min_conn_interval,max_conn_interval,slave_latency,conn_sup_timeout;} ble_gap_conn_params_t;
typedef struct {struct {uint16_t evt_id;} header; struct {struct {
 uint16_t conn_handle; union {struct {ble_gap_conn_params_t conn_params;} connected;
 struct {ble_gap_conn_params_t conn_params;} conn_param_update;} params;
} gap_evt;} evt;} ble_evt_t;
uint32_t sd_ble_gap_conn_param_update(uint16_t,const ble_gap_conn_params_t *);
''',
            'nrf_error.h': '#define NRF_SUCCESS 0\n',
            'timer.h': '#include <stdint.h>\nuint32_t timer_read32(void);\nuint32_t timer_elapsed32(uint32_t);\n',
            'app_util_platform.h': '#define CRITICAL_REGION_ENTER() {\n#define CRITICAL_REGION_EXIT() }\n',
        }
        source = r'''
#include <assert.h>
#include <stdlib.h>
#include "SOURCE"
static uint32_t now;
static bool power_idle, reconnect_during_svc;
static unsigned calls;
static uint32_t result;
static ble_gap_conn_params_t last;
uint32_t timer_read32(void){return now&0x7ffff;}
uint32_t timer_elapsed32(uint32_t at){return (timer_read32()-at)&0x7ffff;}
bool nrf5_wireless_idle(void){return power_idle;}
static void event(unsigned type, unsigned handle, ble_gap_conn_params_t params) {
 ble_evt_t e={.header.evt_id=type,.evt.gap_evt.conn_handle=handle};
 e.evt.gap_evt.params.connected.conn_params=params;nrf5_ble_power_event(&e);
}
uint32_t sd_ble_gap_conn_param_update(uint16_t handle,const ble_gap_conn_params_t *params){
 assert(handle==7);calls++;last=*params;
 assert(params->min_conn_interval==6 && params->max_conn_interval==16);
 assert(params->conn_sup_timeout*10 > 2*(params->slave_latency+1)*params->max_conn_interval*1.25);
 if(reconnect_during_svc){reconnect_during_svc=false;event(BLE_GAP_EVT_DISCONNECTED,7,last);event(BLE_GAP_EVT_CONNECTED,7,profiles[0]);}
 return result;
}
static void advance(unsigned ticks){now+=ticks;nrf5_ble_power_task();}
int main(int argc,char **argv){
 unsigned scenario=atoi(argv[1]);
 nrf5_ble_power_task();assert(!calls);
 now=0x7ffff-1000;event(BLE_GAP_EVT_CONNECTED,7,profiles[0]);nrf5_ble_power_task();
 if(scenario==3){
  power_idle=true;
  for(unsigned i=0;i<3600;i++){advance(1024);}
  assert(!calls && connection==7 && target==0);
  power_idle=false;advance(1024);assert(!calls);
  // If the host reconnects with R16-like latency, request baseline parameters.
  event(BLE_GAP_EVT_DISCONNECTED,7,profiles[1]);event(BLE_GAP_EVT_CONNECTED,7,profiles[1]);
  nrf5_ble_power_task();advance(5*1024);
  assert(calls==1 && last.slave_latency==3 && last.conn_sup_timeout==43);
  event(BLE_GAP_EVT_CONN_PARAM_UPDATE,7,last);power_idle=true;advance(40*1024);assert(calls==1);
  return 0;
 }
 power_idle=true;advance(5*1024-1);assert(!calls);advance(1);assert(calls==1);
 assert(last.slave_latency==9 && last.conn_sup_timeout==100);
 if(scenario==0){
  event(BLE_GAP_EVT_CONN_PARAM_UPDATE,7,last);nrf5_ble_power_task();assert(calls==1);
  for(unsigned i=0;i<7200;i++){advance(1024);}
  assert(calls==1 && connection==7); // two hours, across many RTC wraps
  power_idle=false;nrf5_ble_power_task();assert(calls==2 && last.slave_latency==3 && last.conn_sup_timeout==43);
  event(BLE_GAP_EVT_CONN_PARAM_UPDATE,7,last);advance(20*1024);assert(calls==2);
  event(BLE_GAP_EVT_DISCONNECTED,7,last);power_idle=true;advance(20*1024);assert(calls==2);
  event(BLE_GAP_EVT_CONNECTED,7,profiles[0]);nrf5_ble_power_task();advance(5*1024);assert(calls==3);
 }else if(scenario==1){
  // An ignored/rejected optimization is bounded; keep the usable connection.
  advance(10*1024);assert(calls==2);advance(10*1024);assert(calls==3);
  for(unsigned i=0;i<100;i++){advance(1024);}
  assert(calls==3 && connection==7);
  power_idle=false;nrf5_ble_power_task();assert(calls==3); // active params already match
  power_idle=true;result=17;nrf5_ble_power_task();assert(calls==4);
  for(unsigned i=0;i<100;i++){advance(1024);}
  assert(calls==8 && connection==7);
 }else{
  event(BLE_GAP_EVT_CONN_PARAM_UPDATE,7,last);
  power_idle=false;result=17;nrf5_ble_power_task();assert(calls==2);
  advance(1023);assert(calls==2);result=0;reconnect_during_svc=true;advance(1);assert(calls==3);
  assert(attempts==0); // stale SVC completion cannot mutate new connection state
  nrf5_ble_power_task();advance(5*1024-1);assert(calls==3);
  power_idle=true;advance(1);assert(calls==4);
 }
}
'''.replace('SOURCE',str(ROOT/'platforms/nrf5/ble_power.c'))
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            for name,text in headers.items():
                (work/name).write_text(text)
            (work/'test.c').write_text(source)
            binary=work/'test'
            command=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                     '-fsanitize=address,undefined','-fno-pie','-no-pie','-I',str(work),
                     '-I',str(ROOT/'platforms/nrf5'),'-DBLE_HID_MIN_INTERVAL=7.5','-DBLE_HID_MAX_INTERVAL=20',
                     '-DBLE_HID_SLAVE_LATENCY=3','-DBLE_HID_TIMEOUT=430',
                     f'-DNRF5_BLE_DYNAMIC_IDLE_ENABLE={int(dynamic)}',str(work/'test.c'),'-o',str(binary)]
            for cmd in (command,[str(binary),str(case)]):
                result=subprocess.run(cmd,capture_output=True,text=True,
                                      env={**os.environ,'ASAN_OPTIONS':'detect_stack_use_after_return=1'})
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_profiles_restore_and_rtc_rollover(self):
        self.run_case(0)

    def test_ignored_and_rejected_requests_remain_connected(self):
        self.run_case(1)

    def test_busy_retry_and_handle_reuse_ignore_stale_svc_completion(self):
        self.run_case(2)

    def test_default_parameters_stay_stable_through_hour_long_idle(self):
        self.run_case(3,dynamic=False)
