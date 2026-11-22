# SPDX-License-Identifier: GPL-2.0-or-later
"""Keyboard-side bridge adapters preserve VIA tokens and backpressure."""
import unittest
import test_esb as helpers

class BridgeServiceTests(unittest.TestCase):
    def test_via_control_midi_routing_backpressure_and_reset(self):
        helpers.EsbTests.run_c(self,r'''
#include <assert.h>
#include <string.h>
#include "esb_bridge.h"
#include "music_stream.h"
#include "features/esb_services.c"
static bool via_busy,control_busy,accept_music=true;
static unsigned via_calls,midi_calls,up_calls,via_resets,music_resets,midi_resets;
static uint8_t saved[32],up_channel,up_length;static uint16_t up_token;
bool nrf_via_receive(const uint8_t *p,uint8_t n,uint8_t source,uint16_t token){
 assert(n==32&&source==2);if(via_busy)return false;memcpy(saved,p,32);via_calls++;up_token=token;return true;
}
void nrf_via_disconnect(uint8_t source){assert(source==2);via_resets++;}
bool nrf5_bridge_send(uint8_t channel,uint16_t token,const uint8_t *p,uint8_t n){up_channel=channel;up_token=token;up_length=n;memcpy(saved,p,n);up_calls++;return true;}
bool nrf5_esb_control_ready(void){return !control_busy;}
bool music_stream_receive(const uint8_t *p,size_t n,music_source_t source){assert(n==20&&source==3);memcpy(saved,p,n);return accept_music;}
void music_stream_disconnect(music_source_t source){assert(source==3);music_resets++;}
void nrf_midi_receive(const uint8_t p[4]){memcpy(saved,p,4);midi_calls++;}
void nrf_midi_reset(void){midi_resets++;}
bool nrf5_esb_via_reply(const uint8_t *,uint16_t);
bool nrf5_esb_control_reply(const uint8_t *);
int main(void){
 uint8_t first[20]={0x12,0x34,0,0},second[20]={0x12,0x34,1,0};
 for(unsigned i=0;i<16;i++){first[i+4]=i;second[i+4]=i+16;}
 assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_VIA,second,20)&&via_calls==0);
 assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_VIA,first,20));
 via_busy=true;assert(!nrf5_bridge_down_deliver(NRF5_ESB_DOWN_VIA,second,20)&&via_calls==0);
 via_busy=false;assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_VIA,second,20)&&via_calls==1&&up_token==0x1234);
 for(unsigned i=0;i<32;i++)assert(saved[i]==i);
 assert(nrf5_esb_via_reply(saved,0x1234)&&up_channel==1&&up_length==32&&up_token==0x1234);
 uint8_t control[20]={0xC7,4,19};control_busy=true;
 assert(!nrf5_bridge_down_deliver(NRF5_ESB_DOWN_MUSIC,control,20));
 control_busy=false;assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_MUSIC,control,20));
 accept_music=false;assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_MUSIC,control,20));
 assert(up_channel==2&&up_length==20&&saved[0]==0xC7&&saved[1]==0xFF&&saved[2]==19);
 assert(nrf5_esb_control_reply(control)&&up_channel==2&&up_length==20);
 uint8_t note[4]={9,0x90,60,100};assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_MIDI,note,4)&&midi_calls==1&&!memcmp(note,saved,4));
 assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_VIA,first,20));nrf5_bridge_link_reset();
 assert(via_resets==1&&music_resets==1&&midi_resets==1);
 assert(nrf5_bridge_down_deliver(NRF5_ESB_DOWN_VIA,second,20)&&via_calls==1);
}
''',headers={
'music_stream.h':r'''#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef enum {MUSIC_SOURCE_USB=1,MUSIC_SOURCE_BLE=2,MUSIC_SOURCE_ESB=3} music_source_t;
bool music_stream_receive(const uint8_t *,size_t,music_source_t);void music_stream_disconnect(music_source_t);
''',
'via_transport.h':'#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#define NRF_VIA_ESB 2\nbool nrf_via_receive(const uint8_t *,uint8_t,uint8_t,uint16_t);\nvoid nrf_via_disconnect(uint8_t);',
'midi_transport.h':'#pragma once\n#include <stdint.h>\nvoid nrf_midi_receive(const uint8_t [4]);void nrf_midi_reset(void);'
},flags=['-DNRF5_ESB_TX','-DRAW_ENABLE','-DMUSIC_STREAM_ENABLE','-DMIDI_ENABLE'])

if __name__=='__main__':unittest.main()
