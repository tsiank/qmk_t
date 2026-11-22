# SPDX-License-Identifier: GPL-2.0-or-later
"""Pair feedback must reflect a verified link and preserve user lighting state."""
import unittest
import test_esb as helpers

class IndicatorTests(unittest.TestCase):
    def test_pair_colors_verified_success_failure_restore_caps_and_disabled_state(self):
        headers={'host_driver.h':helpers.HOST,
          'quantum.h':'#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\ntypedef struct {bool pressed;} keyrecord_t;',
          'rgblight.h':r'''#pragma once
#include "quantum.h"
#define RGBLIGHT_MAX_LAYERS 8
#define RGBLIGHT_LED_COUNT 18
#define RGBLIGHT_END_SEGMENTS {255,0,0,0,0}
#define pgm_read_ptr(p) (*(p))
typedef struct {uint8_t index,count,hue,sat,val;} rgblight_segment_t;
extern const rgblight_segment_t *const *rgblight_layers;
void rgblight_set_layer_state(uint8_t,bool);
bool rgblight_is_enabled(void);
void rgblight_mode_noeeprom(uint8_t);
uint8_t rgblight_get_mode(void);
void rgblight_set(void);
'''}
        helpers.EsbTests.run_c(self,r'''
#include <assert.h>
#include "features/esb_indicator.c"
static uint32_t clock_ms;
static uint8_t pair_phase,mask,render_hue,render_value;
static bool enabled,connected,power;
static unsigned writes,restores;
static const rgblight_segment_t caps[]={{9,8,0,0,96},RGBLIGHT_END_SEGMENTS};
static const rgblight_segment_t *const base[]={caps,NULL};
const rgblight_segment_t *const *rgblight_layers=base;
uint32_t nrf5_esb_now(void){return clock_ms;}
uint8_t nrf5_pair_state(void){return pair_phase;}
bool nrf5_esb_connected(void){return connected;}
bool nrf5_underglow_test_is_active(void){return false;}
void rgblight_set_layer_state(uint8_t i,bool on){if(on)mask|=1<<i;else mask&=~(1<<i);}
bool rgblight_is_enabled(void){return enabled;}
uint8_t rgblight_get_mode(void){return 7;}
void rgblight_set(void){
 writes++;power=enabled||mask;render_value=0;
 if(owned){assert(rgblight_layers[slot][0].index==0&&rgblight_layers[slot][0].count==18);
 render_hue=rgblight_layers[slot][0].hue;render_value=rgblight_layers[slot][0].val;}
}
void rgblight_mode_noeeprom(uint8_t mode){assert(mode==7);restores++;rgblight_set();}
static void frame(uint8_t phase,uint32_t time){pair_phase=phase;clock_ms=time;nrf5_esb_indicator_task();}
int main(void){
 frame(0,0);assert(!writes&&!owned);
 frame(1,10);assert(owned&&render_hue==170&&render_value==96&&power&&!enabled);
 assert(rgblight_layers[0]==caps&&mask==2);
 frame(1,310);assert(render_value==0&&power);
 rgblight_set_layer_state(0,true);frame(2,400);assert(render_hue==32&&render_value==96&&mask==3);
 frame(4,500);frame(5,600);assert(render_hue==32); // stored is not yet connected
 connected=true;frame(5,700);assert(render_hue==85&&render_value==96);
 frame(5,900);assert(render_value==0);frame(5,1900);assert(!owned&&rgblight_layers==base&&!enabled&&mask==1);
 // Current Caps state, rather than the state at pairing start, is retained.
 enabled=true;frame(1,2000);frame(7,2100);assert(render_hue==0&&render_value==96);
 rgblight_set_layer_state(0,false);frame(7,3300);assert(!owned&&mask==0&&rgblight_layers==base&&restores==1&&enabled);
 // Saving keys without ever establishing the encrypted input link is a failure.
 enabled=false;connected=false;frame(1,4000);frame(5,4100);assert(render_hue==32);
 frame(5,9100);assert(render_hue==0&&render_value==96);frame(5,10300);
 assert(!owned&&!power&&!enabled&&rgblight_layers==base);
 // A new attempt can show success again; no stale terminal state masks it.
 frame(1,11000);connected=true;frame(5,11100);assert(render_hue==85);
 frame(8,11200);assert(render_hue==32);frame(8,12400);assert(!owned&&!power);
}
''',headers,flags=['-DRGBLIGHT_LAYERS'])

if __name__=='__main__':unittest.main()
