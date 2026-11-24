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
static uint32_t scan_interval=6,clock_ticks,last_radio,due,override_delay;
static unsigned starts,stops,created,expiries;
static bool armed;
static void (*callback)(void *);
uint8_t nrf5_output_mode(void){return mode;}
uint32_t timer_read32(void){return clock_ticks & 0x7ffffu;}
uint32_t nrf5_wireless_scan_interval(void){return scan_interval;}
uint32_t nrf5_esb_next_wake_ticks(void){
 if(override_delay)return override_delay;
 uint32_t elapsed=clock_ticks-last_radio;return elapsed>=100?1:100-elapsed;
}
int app_timer_create(unsigned *id,unsigned kind,void (*cb)(void *)){
 assert(kind==0);*id=1;callback=cb;created++;return 0;
}
int app_timer_start(unsigned id,unsigned ms,void *ctx){
 assert(id==1&&ms>0&&!ctx&&!armed);armed=true;starts++;
 due=clock_ticks+(ms*1024+999)/1000;return 0;
}
int app_timer_stop(unsigned id){assert(id==1&&armed);armed=false;stops++;return 0;}
int main(void){
 nrf5_esb_wake_init();assert(created==1&&!armed);
 nrf5_esb_wake_update();assert(!starts); // active BLE
 scan_interval=1000;nrf5_esb_wake_update();assert(!starts); // idle BLE
 mode=3;scan_interval=6;nrf5_esb_wake_update();assert(!starts); // active ESB
 scan_interval=1000;nrf5_esb_wake_update();assert(starts==1&&armed);
 unsigned scans=0,radio=0;
 for(clock_ticks=1;clock_ticks<=90*1024;clock_ticks++){
  if(armed&&clock_ticks>=due){armed=false;expiries++;callback(NULL);}
  // Unrelated IRQs must not keep pushing the radio deadline into the future.
  if(clock_ticks%17==0)nrf5_esb_wake_update();
  if(clock_ticks%1024==0||nrf5_esb_wake_pending()){
   nrf5_esb_wake_begin();assert(!nrf5_esb_wake_pending());
   if(clock_ticks%1024==0)scans++;
   if(clock_ticks-last_radio>=100){last_radio=clock_ticks;radio++;}
   nrf5_esb_wake_update();
  }
  assert(clock_ticks-last_radio<103); // comfortably below 300-tick supervision
 }
 assert(scans==90&&radio>=900&&expiries<=1020); // fixed 40 needed ~2304 IRQs
 // Shorter deadlines rearm; longer ones keep the earlier wake already queued.
 scan_interval=6;nrf5_esb_wake_update();
 override_delay=100;scan_interval=1000;nrf5_esb_wake_update();
 override_delay=10;unsigned old_starts=starts;nrf5_esb_wake_update();
 assert(starts==old_starts+1&&armed);uint32_t earlier=due;
 override_delay=100;nrf5_esb_wake_update();assert(due==earlier&&starts==old_starts+1);
 scan_interval=6;nrf5_esb_wake_update();assert(!armed); // input/music
 scan_interval=1000;nrf5_esb_wake_update();assert(armed);
 mode=1;nrf5_esb_wake_update();assert(!armed); // USB
 // An IRQ already queued before stop must not restart the timer on other routes.
 callback(NULL);assert(nrf5_esb_wake_pending());nrf5_esb_wake_begin();nrf5_esb_wake_update();assert(!armed&&!nrf5_esb_wake_pending());
 mode=2;nrf5_esb_wake_update();assert(!armed); // BLE
 // Raw QMK RTC rollover must not postpone an armed deadline.
 mode=3;clock_ticks=0x7ffff-30;override_delay=100;nrf5_esb_wake_update();earlier=due;
 clock_ticks+=50;nrf5_esb_wake_update();assert(due==earlier);
}

''', {
 'host_driver.h':helpers.HOST,
 'outputselect.h':'#include <stdint.h>\nuint8_t nrf5_output_mode(void);',
 'timer.h':'#include <stdint.h>\nuint32_t timer_read32(void);',
 'app_util_platform.h':'#define CRITICAL_REGION_ENTER() do {} while(0)\n#define CRITICAL_REGION_EXIT() do {} while(0)',
 'nrf.h':'#pragma once\nstatic inline void __SEV(void){}',
 'app_error.h':'#include <assert.h>\n#define APP_ERROR_CHECK(x) assert((x)==0)',
 'app_timer.h':r'''#pragma once
#include <stdint.h>
#include <stddef.h>
#define APP_TIMER_DEF(name) static unsigned name
#define APP_TIMER_MODE_SINGLE_SHOT 0
#define APP_TIMER_TICKS(ms) (ms)
int app_timer_create(unsigned *,unsigned,void (*)(void *));
int app_timer_start(unsigned,unsigned,void *);
int app_timer_stop(unsigned);
''',
 }, flags=['-DNRF5_ESB_TX','-DNRF5_WIRELESS_POWER_ENABLE'])
