// SPDX-License-Identifier: GPL-2.0-or-later
// Keyboard-side services execute in the main loop, never in RADIO callbacks.
#include "esb_bridge.h"
#include <string.h>
#ifdef RAW_ENABLE
#    include "via_transport.h"
static uint8_t via_request[32];
static uint16_t via_token;
static bool via_first;
bool nrf5_esb_via_reply(const uint8_t *data,uint16_t token) { return nrf5_bridge_send(NRF5_BRIDGE_VIA,token,data,32); }
#endif
#ifdef MUSIC_STREAM_ENABLE
#    include "music_stream.h"
bool nrf5_esb_control_reply(const uint8_t *data) { return nrf5_bridge_send(NRF5_BRIDGE_CONTROL,0,data,20); }
#endif
#ifdef MIDI_ENABLE
#    include "midi_transport.h"
#endif
bool nrf5_bridge_down_deliver(uint8_t kind,const uint8_t *data,uint8_t length) {
    switch(kind) {
#ifdef RAW_ENABLE
        case NRF5_ESB_DOWN_VIA: {
            if(length!=20||data[2]>1||data[3])return true;
            uint16_t token=(uint16_t)data[0]<<8|data[1];
            if(!data[2]) { via_token=token;memcpy(via_request,data+4,16);via_first=true;return true; }
            if(!via_first||via_token!=token)return true;
            memcpy(via_request+16,data+4,16);
            if(!nrf_via_receive(via_request,32,NRF_VIA_ESB,token))return false;
            via_first=false;return true;
        }
#endif
#ifdef MUSIC_STREAM_ENABLE
        case NRF5_ESB_DOWN_MUSIC:
            if(data[1]>=3 && data[1]<=6) {
                bool nrf5_esb_control_ready(void);
                if(!nrf5_esb_control_ready())return false;
            }
            if(music_stream_receive(data,length,MUSIC_SOURCE_ESB))return true;
            if(data[1]>=3 && data[1]<=6) {
                uint8_t error[20]={0xC7,0xFF,data[2]};
                return nrf5_bridge_send(NRF5_BRIDGE_CONTROL,0,error,20);
            }
            return true;
#endif
#ifdef MIDI_ENABLE
        case NRF5_ESB_DOWN_MIDI:
            if(length==4)nrf_midi_receive(data);
            return true;
#endif
        default:return true;
    }
}
void nrf5_bridge_link_reset(void) {
#ifdef RAW_ENABLE
    via_first=false;nrf_via_disconnect(NRF_VIA_ESB);
#endif
#ifdef MUSIC_STREAM_ENABLE
    music_stream_disconnect(MUSIC_SOURCE_ESB);
#endif
#ifdef MIDI_ENABLE
    nrf_midi_reset();
#endif
}
