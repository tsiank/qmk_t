# SPDX-License-Identifier: GPL-2.0-or-later
"""Standby must service radio without restoring frequent matrix scans."""
import unittest
import test_esb as helpers

class EsbWakeTests(unittest.TestCase):
    def test_radio_supervision_survives_slow_matrix_without_waking_other_routes(self):
        helpers.EsbTests().run_c(r'''
#include <assert.h>
#include "esb_wake.c"
static uint8_t mode=2;
static uint32_t scan_interval=6;
static unsigned starts,stops,created;
static bool armed;
static void (*callback)(void *);
uint8_t nrf5_output_mode(void){return mode;}
uint32_t nrf5_wireless_scan_interval(void){return scan_interval;}
int app_timer_create(unsigned *id,unsigned kind,void (*cb)(void *)){
 assert(kind==1);*id=1;callback=cb;created++;return 0;
}
int app_timer_start(unsigned id,unsigned ticks,void *ctx){assert(id==1&&ticks==40&&!ctx&&!armed);armed=true;starts++;return 0;}
int app_timer_stop(unsigned id){assert(id==1&&armed);armed=false;stops++;return 0;}
int main(void){
 nrf5_esb_wake_init();assert(created==1&&!armed);
 nrf5_esb_wake_update();assert(!starts); // active BLE
 scan_interval=1000;nrf5_esb_wake_update();assert(!starts); // idle BLE
 mode=3;scan_interval=6;nrf5_esb_wake_update();assert(!starts); // active ESB
 scan_interval=1000;nrf5_esb_wake_update();assert(starts==1&&armed);
 uint32_t last_radio=0;unsigned scans=0,radio=0;
 for(unsigned now=1;now<=90000;now++){
  if(armed&&now%40==0)callback(NULL);
  if(now%1000==0||nrf5_esb_wake_pending()){
   nrf5_esb_wake_begin();assert(!nrf5_esb_wake_pending());
   if(now%1000==0)scans++;
   if(now-last_radio>=100){last_radio=now;radio++;}
   nrf5_esb_wake_update();
  }
  assert(now-last_radio<300); // receiver supervision must never expire
 }
 assert(scans==90&&radio>=700&&starts==1&&!stops);
 scan_interval=6;nrf5_esb_wake_update();assert(stops==1&&!armed); // input or music
 scan_interval=1000;nrf5_esb_wake_update();assert(starts==2&&armed);
 mode=1;nrf5_esb_wake_update();assert(stops==2&&!armed); // USB
 // An IRQ already queued before stop must be consumed without restarting it.
 callback(NULL);assert(nrf5_esb_wake_pending());nrf5_esb_wake_begin();nrf5_esb_wake_update();assert(!armed&&!nrf5_esb_wake_pending());
 mode=2;nrf5_esb_wake_update();assert(starts==2&&!armed); // BLE
}
''', {
 'host_driver.h':helpers.HOST,
 'outputselect.h':'#include <stdint.h>\nuint8_t nrf5_output_mode(void);',
 'nrf.h':'#pragma once\nstatic inline void __SEV(void){}',
 'app_error.h':'#include <assert.h>\n#define APP_ERROR_CHECK(x) assert((x)==0)',
 'app_timer.h':r'''#pragma once
#include <stdint.h>
#include <stddef.h>
#define APP_TIMER_DEF(name) static unsigned name
#define APP_TIMER_MODE_REPEATED 1
#define APP_TIMER_TICKS(ms) (ms)
int app_timer_create(unsigned *,unsigned,void (*)(void *));
int app_timer_start(unsigned,unsigned,void *);
int app_timer_stop(unsigned);
''',
 }, flags=['-DNRF5_ESB_TX','-DNRF5_WIRELESS_POWER_ENABLE'])
