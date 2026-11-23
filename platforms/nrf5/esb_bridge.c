// SPDX-License-Identifier: GPL-2.0-or-later
#include "esb_bridge.h"
#include "esb_crypto.h"
#include <string.h>
#ifdef NRF5_ESB_TX
#    include "timer.h"
#endif
#define BRIDGE_QUEUE 32
static inline uint32_t read32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p,uint32_t value) { for(unsigned i=0;i<4;i++)p[i]=value>>(24-8*i); }
__attribute__((weak)) bool nrf5_bridge_down_deliver(uint8_t kind,const uint8_t *p,uint8_t n) { (void)kind;(void)p;(void)n;return false; }
__attribute__((weak)) bool nrf5_bridge_up_deliver(uint8_t kind,uint16_t token,const uint8_t *p,uint8_t n) { (void)kind;(void)token;(void)p;(void)n;return false; }
__attribute__((weak)) void nrf5_bridge_link_reset(void) {}
#ifdef NRF5_ESB_TX
static nrf5_esb_report_t up[BRIDGE_QUEUE];
static uint8_t head,count;
static uint32_t received,confirmed,offered;
static bool offered_ack, service_active;
static uint32_t service_at;
void nrf5_bridge_reset(void) { service_active=false;head=count=0;received=confirmed=offered=0;nrf5_bridge_link_reset(); }
void nrf5_bridge_abort(void) { service_active=false;head=count=0;nrf5_bridge_link_reset(); }
void nrf5_bridge_tick(void) {
    if (service_active && ((timer_read32() - service_at) & 0x7FFFFu) >= 1024) service_active = false;
}
bool nrf5_bridge_fast_poll(void) { return service_active || count || received != confirmed; }
bool nrf5_bridge_send(uint8_t channel,uint16_t token,const uint8_t *data,uint8_t length) {
    if (!data || !length || length>32 || count+(length>24?2:1)>BRIDGE_QUEUE) return false;
    service_active = true; service_at = timer_read32();
    for(uint8_t offset=0;offset<length;offset+=24) {
        nrf5_esb_report_t *r=&up[(head+count)%BRIDGE_QUEUE];
        uint8_t size=length-offset;if(size>24)size=24;
        r->kind=NRF5_ESB_BRIDGE;r->length=8+size;
        r->data[0]=channel;r->data[1]=(offset?1:0)|((offset+size==length)?0x80:0);
        r->data[2]=token>>8;r->data[3]=token;
        put32(r->data+4,received);memcpy(r->data+8,data+offset,size);count++;
    }
    return true;
}
bool nrf5_bridge_next(nrf5_esb_report_t *r) {
    offered=received;offered_ack=false;
    if(count) { *r=up[head];put32(r->data+4,received);return true; }
    if(received!=confirmed) { r->kind=NRF5_ESB_BRIDGE_ACK;r->length=4;put32(r->data,received);offered_ack=true;return true; }
    return false;
}
void nrf5_bridge_sent(void) {
    confirmed=offered;
    if(!offered_ack&&count) { head=(head+1)%BRIDGE_QUEUE;count--; }
}
void nrf5_bridge_down(const uint8_t ext[34],const uint8_t key[16],uint64_t session,uint64_t challenge) {
    uint32_t sequence=read32(ext);
    if(!sequence||(!nrf5_esb_newer(sequence,received)&&sequence!=received)||ext[5]>20) return;
    nrf5_esb_report_t r={.kind=ext[4],.length=ext[5]};uint8_t p[64];
    uint8_t length=nrf5_esb_encode(p,session,sequence,&r);
    if(!length || r.kind<NRF5_ESB_DOWN_VIA || r.kind>NRF5_ESB_DOWN_MIDI) return;
    for(unsigned i=6+r.length;i<26;i++)if(ext[i])return;
    memcpy(p+16,ext+6,r.length);memcpy(p+16+r.length,ext+26,8);
    if(!nrf5_esb_open(key,challenge,p,length))return;
    service_active = true; service_at = timer_read32();
    if(sequence==received)return;
    if(nrf5_bridge_down_deliver(r.kind,p+16,r.length))received=sequence;
}
#else
static nrf5_esb_report_t down[BRIDGE_QUEUE],latest_frame;
static uint8_t head,count;
static uint32_t sequence;
static bool frame_ready,waiting;
static nrf5_esb_report_t current;
static nrf5_esb_report_t up[BRIDGE_QUEUE];
static uint8_t up_head,up_count;
static uint8_t assembled[32],assembled_size,assembled_channel;
static uint16_t assembled_token;
static bool assembling;
void nrf5_bridge_reset(void) {
    head=count=up_head=up_count=0;sequence=0;frame_ready=waiting=assembling=false;nrf5_bridge_link_reset();
}
void nrf5_bridge_abort(void) { head=count=up_head=up_count=0;frame_ready=waiting=assembling=false;nrf5_bridge_link_reset(); }
bool nrf5_bridge_submit(uint8_t kind,const uint8_t *data,uint8_t length,bool latest) {
    if(!data||kind<NRF5_ESB_DOWN_VIA||kind>NRF5_ESB_DOWN_MIDI||length!=(kind==NRF5_ESB_DOWN_MIDI?4:20)) return false;
    if(!latest&&count==BRIDGE_QUEUE)return false;
    // A reliable STOP must not be followed by an older coalesced music frame.
    if(kind==NRF5_ESB_DOWN_MUSIC&&data[1]==1&&!(data[3]&3))frame_ready=false;
    nrf5_esb_report_t *r=latest?&latest_frame:&down[(head+count)%BRIDGE_QUEUE];
    r->kind=kind;r->length=length;memcpy(r->data,data,length);
    if(latest)frame_ready=true;else count++;
    return true;
}
bool nrf5_bridge_via_request(uint16_t token,const uint8_t data[32]) {
    if(count>BRIDGE_QUEUE-2)return false;
    for(unsigned part=0;part<2;part++) {
        uint8_t fragment[20]={token>>8,token,part,0};memcpy(fragment+4,data+16*part,16);
        (void)nrf5_bridge_submit(NRF5_ESB_DOWN_VIA,fragment,20,false);
    }
    return true;
}
void nrf5_bridge_confirm(uint32_t seq) { if(waiting&&seq==sequence)waiting=false; }
bool nrf5_bridge_extension(uint8_t ext[34],const uint8_t key[16],uint64_t session,uint64_t challenge) {
    if(!waiting) {
        if(count) { current=down[head];head=(head+1)%BRIDGE_QUEUE;count--; }
        else if(frame_ready) { current=latest_frame;frame_ready=false; }
        else return false;
        // Reconnect before wrapping, so a key/nonce pair is never reused.
        if(sequence==0xFFFFFFFFu)return false;
        sequence++;waiting=true;
    }
    uint8_t p[64];uint8_t length=nrf5_esb_encode(p,session,sequence,&current);
    if(!length||!nrf5_esb_seal(key,challenge,p,length))return false;
    memset(ext,0,34);put32(ext,sequence);ext[4]=current.kind;ext[5]=current.length;
    memcpy(ext+6,p+16,current.length);memcpy(ext+26,p+16+current.length,8);return true;
}
bool nrf5_bridge_up_ready(void) { return up_count<BRIDGE_QUEUE; }
void nrf5_bridge_up(const nrf5_esb_report_t *r) { if(up_count<BRIDGE_QUEUE) { up[(up_head+up_count)%BRIDGE_QUEUE]=*r;up_count++; } }
void nrf5_bridge_tick(void) {
    if(!up_count)return;
    const nrf5_esb_report_t *r=&up[up_head];const uint8_t *p=r->data;uint8_t length=r->length-8;
    uint16_t token=(uint16_t)p[2]<<8|p[3];
    bool valid=!(p[1]&0x7e) && p[0]>=1 && p[0]<=3;
    if(!(p[1]&1)) { assembled_size=0;assembled_channel=p[0];assembled_token=token;assembling=true; }
    else valid &= assembling&&assembled_channel==p[0]&&assembled_token==token&&assembled_size==24;
    valid &= assembled_size+length<=32;
    if(valid) {
        memcpy(assembled+assembled_size,p+8,length);
        if(p[1]&0x80) {
            if(!nrf5_bridge_up_deliver(p[0],token,assembled,assembled_size+length))return;
            assembling=false;assembled_size=0;
        } else assembled_size+=length;
    } else assembling=false;
    up_head=(up_head+1)%BRIDGE_QUEUE;up_count--;
}
#endif
