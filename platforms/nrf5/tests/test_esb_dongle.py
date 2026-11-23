# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual dedicated receiver USB implementation with SDK callbacks.

The host fixture checks ownership/ordering; USB enumeration and RF timing still
require hardware. No QMK matrix, VIA, OLED or BLE service mocks are provided.
"""
import unittest
import test_esb as helpers

USB = r'''#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "host_driver.h"
typedef int ret_code_t;
typedef int app_usbd_hid_subclass_desc_t;
typedef enum {APP_USBD_HID_USER_EVT_IN_REPORT_DONE,APP_USBD_HID_USER_EVT_OUT_REPORT_READY,
 APP_USBD_HID_USER_EVT_SET_BOOT_PROTO,APP_USBD_HID_USER_EVT_SET_REPORT_PROTO} app_usbd_hid_user_event_t;
typedef struct hid app_usbd_class_inst_t;
typedef struct hid {int id;void (*event)(const app_usbd_class_inst_t *,app_usbd_hid_user_event_t);
 const app_usbd_hid_subclass_desc_t **desc;} app_usbd_hid_generic_t;
typedef enum {APP_USBD_EVT_DRV_RESET,APP_USBD_EVT_DRV_SUSPEND,APP_USBD_EVT_STOPPED,
 APP_USBD_EVT_POWER_DETECTED,APP_USBD_EVT_POWER_READY,APP_USBD_EVT_POWER_REMOVED} app_usbd_event_type_t;
typedef struct {void (*ev_state_proc)(app_usbd_event_type_t);bool enable_sof;} app_usbd_config_t;
#define APP_USBD_HID_GENERIC_SUBCLASS_REPORT_DESC(name,...) static const app_usbd_hid_subclass_desc_t name=0
#define APP_USBD_HID_GENERIC_GLOBAL_DEF(name,iface,event,ep,desc,...) static const app_usbd_hid_generic_t name={iface,event,desc}
#define NRF_SUCCESS 0
#define APP_USBD_STATE_Configured 3
#define APP_ERROR_CHECK(x) assert((x)==0)
#define UNUSED_RETURN_VALUE(x) (void)(x)
static const app_usbd_hid_subclass_desc_t kbd_desc=0,kbd2_desc=0,mouse_desc=0,extrakey_desc=0;
static int core_state=3,submit_error,clears,jumps,appends;
static bool power_enabled,started,suspended;
static const uint8_t *dma[5];
static unsigned dma_size[5];
static uint8_t output_report[33];static size_t output_length;
static const app_usbd_class_inst_t *app_usbd_hid_generic_class_inst_get(const app_usbd_hid_generic_t *h){return h;}
static const app_usbd_hid_generic_t *app_usbd_hid_generic_class_get(const app_usbd_class_inst_t *h){return h;}
static int app_usbd_core_state_get(void){return core_state;}
static const uint8_t *app_usbd_hid_generic_out_report_get(const app_usbd_hid_generic_t *h,size_t *n){(void)h;*n=output_length;return output_report;}
static int app_usbd_hid_generic_in_report_set(const app_usbd_hid_generic_t *h,const void *data,unsigned n){
 assert(!dma[h->id]);dma[h->id]=data;dma_size[h->id]=n;return submit_error;
}
static int hid_generic_clear_buffer(const app_usbd_class_inst_t *h){dma[h->id]=NULL;clears++;return 0;}
static int app_usbd_init(const app_usbd_config_t *c){assert(c->ev_state_proc&&!c->enable_sof);return 0;}
static int app_usbd_class_append(const app_usbd_class_inst_t *h){assert(h->id==appends);appends++;return 0;}
static int app_usbd_power_events_enable(void){return 0;}
static void app_usbd_suspend_req(void){suspended=true;}
static void app_usbd_disable(void){power_enabled=false;}
static void app_usbd_enable(void){power_enabled=true;}
static bool nrf_drv_usbd_is_enabled(void){return power_enabled;}
static void app_usbd_start(void){started=true;}
static void app_usbd_stop(void){started=false;}
'''

class DongleTests(unittest.TestCase):
    def test_startup_initializes_power_before_softdevice_and_usb(self):
        headers={name:'#pragma once\n#include "fixture.h"\n' for name in (
            'nrf.h','nrf_sdh.h','nrf_drv_clock.h','nrf_drv_power.h','app_timer.h','app_usbd.h','app_error.h')}
        headers['host_driver.h']=helpers.HOST
        headers['fixture.h']=r'''#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
static jmp_buf done;
static int initialized;
static bool sd,usb,clock_ok,power_ok,timer_ok;
#define APP_ERROR_CHECK(x) assert((x)==0)
#define APP_TIMER_DEF(name) static int name
#define APP_TIMER_MODE_REPEATED 1
#define APP_TIMER_TICKS(ms) (ms)
static int nrf_drv_clock_init(void){clock_ok=true;return 0;}
static int nrf_drv_power_init(void *config){assert(!config);if(sd)return 8;power_ok=true;return 0;}
static int nrf_sdh_enable_request(void){assert(clock_ok&&power_ok);sd=true;return 0;}
static int app_timer_init(void){assert(sd);timer_ok=true;return 0;}
static int app_timer_create(int *id,int mode,void (*callback)(void *)){assert(timer_ok&&id&&mode==1&&callback);return 0;}
static int app_timer_start(int id,int ticks,void *ctx){(void)id;assert(ticks==10&&!ctx);return 0;}
static bool app_usbd_event_queue_process(void){return false;}
static int sd_app_evt_wait(void){assert(initialized==1&&usb);longjmp(done,1);return 0;}
'''
        helpers.EsbTests.run_c(self,r'''
#define main dongle_main
#include "dongle/main.c"
#undef main
void nrf5_dongle_usb_init(void){assert(sd&&power_ok);usb=true;}
void nrf5_dongle_usb_task(void){assert(initialized);}
void nrf5_esb_init(void){assert(usb);initialized++;}
void nrf5_esb_task(void){assert(initialized);}
int main(void){if(!setjmp(done))dongle_main();assert(power_ok&&sd&&usb&&initialized==1);}
''',headers)

    def test_receiver_usb_dma_queue_leds_suspend_and_dfu_acknowledgement(self):
        helpers.EsbTests.run_c(self, r'''
#include <assert.h>
#include <string.h>
#include "dongle/usb.c"
static uint32_t now;
static bool pairing_busy,flash_pending;
static int queries,windows;
void nrf5_esb_radio_diagnostics(uint8_t p[32]){p[1]=1;p[2]=2;}
static bool rf_connected;
static unsigned midi_appends,forwarded;static uint8_t forward[32],forward_kind,forward_length;static bool forward_latest,midi_block;static uint8_t midi_sent[4];
bool nrf5_esb_connected(void){return rf_connected;}
bool nrf5_bridge_submit(uint8_t kind,const uint8_t *data,uint8_t length,bool latest){forward_kind=kind;forward_length=length;forward_latest=latest;memcpy(forward,data,length);forwarded++;return true;}
bool nrf5_bridge_via_request(uint16_t token,const uint8_t data[32]){(void)token;memcpy(forward,data,32);forwarded++;return true;}
void nrf5_bridge_abort(void){nrf5_bridge_link_reset();}
void nrf5_esb_reset_link(void){rf_connected=false;nrf5_bridge_abort();}
void nrf_midi_usb_init(void){midi_appends++;}
void nrf_midi_usb_task(void){}
void nrf_midi_usb_panic(void){}
bool nrf_midi_usb_send(const uint8_t packet[4]){if(midi_block)return false;memcpy(midi_sent,packet,4);return true;}

void nrf5_pair_usb_window(void){windows++;}
uint32_t timer_read32(void){return now;}
uint32_t timer_elapsed32(uint32_t last){return now-last;}
void bootloader_jump(void){jumps++;}
bool nrf5_pair_busy(void){return pairing_busy;}
bool nrf5_esb_settings_pending(void){return flash_pending;}
void nrf5_esb_diagnostics(uint8_t p[32]){memset(p,0,32);p[0]=0x83;p[1]=1;}
void nrf5_esb_command(uint8_t p[32],bool usb){
 assert(usb);uint8_t a=p[30],b=p[31];uint8_t error=p[2]!=1||p[1]>6||p[3];
 for(int i=4;i<30;i++)error|=p[i]!=0;
 queries++;memset(p,0,32);p[0]=0x84;p[1]=2;p[2]=error;p[3]=2;p[25]=a;p[26]=b;
}
static void complete(const app_usbd_hid_generic_t *h){assert(dma[h->id]);dma[h->id]=NULL;h->event(h,APP_USBD_HID_USER_EVT_IN_REPORT_DONE);}
static void request(uint8_t cmd){memset(output_report,0,33);output_report[1]=0x84;output_report[2]=cmd;output_report[3]=1;output_report[31]=0x12;output_report[32]=0x34;output_length=33;control_event(&control,APP_USBD_HID_USER_EVT_OUT_REPORT_READY);}
int main(void){
 nrf5_dongle_usb_init();assert(appends==5&&midi_appends==1);
 usb_event(APP_USBD_EVT_POWER_DETECTED);usb_event(APP_USBD_EVT_POWER_READY);assert(power_enabled&&started);
 uint8_t data[32]={0,0,4};
 assert(nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));assert(dma[0]!=data&&dma[0][2]==4);
 data[2]=9;assert(!nrf5_usb_esb_report(NRF5_ESB_MOUSE,data,5));assert(dma[0][2]==4);
 // Real interrupt OUT: getter returns capacity 33 but payload starts at [0].
 memset(output_report,0,33);output_report[0]=0x84;output_report[2]=1;
 output_report[30]=0x12;output_report[31]=0x34;output_report[32]=0xA5;output_length=33;
 control_event(&control,APP_USBD_HID_USER_EVT_OUT_REPORT_READY);nrf5_dongle_usb_task();
 assert(dma[4]&&dma[4][0]==0x84&&dma[4][25]==0x12&&dma[4][26]==0x34);complete(&control);
 request(0);nrf5_dongle_usb_task();assert(windows==1);assert(dma[4]&&dma[4][25]==0x12&&dma[4][26]==0x34);
 assert(dma[0][2]==4);complete(&control);complete(&kbd);
 assert(nrf5_usb_esb_report(NRF5_ESB_NKRO,data,31));assert(dma_size[1]==31);complete(&nkro);
 assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,data,5));assert(dma_size[2]==5);complete(&mouse);
 // Empty periodic mouse snapshots must not look like fresh Windows input.
 uint8_t motion[5]={0};
 assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&!dma[2]);
 assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&!dma[2]);
 motion[0]=1;assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&dma[2]);complete(&mouse);
 assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&!dma[2]);
 motion[0]=0;assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&dma[2]);complete(&mouse);
 assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&!dma[2]);
 for(unsigned i=1;i<5;i++){
  motion[i]=1;
  for(unsigned repeat=0;repeat<2;repeat++){
   assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&dma[2]);complete(&mouse);
  }
  motion[i]=0;
 }
 assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,data,3));assert(dma_size[3]==3);complete(&extra);
 // Alternating periodic system/consumer releases are independently deduplicated.
 uint8_t usage[3]={REPORT_ID_SYSTEM,0,0};
 assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&dma[3]);complete(&extra);
 usage[0]=REPORT_ID_CONSUMER;assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&dma[3]);complete(&extra);
 for(unsigned i=0;i<4;i++){
  usage[0]=i%2?REPORT_ID_SYSTEM:REPORT_ID_CONSUMER;
  assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&!dma[3]);
 }
 usage[0]=REPORT_ID_CONSUMER;usage[1]=0xE9;
 assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&dma[3]);complete(&extra);
 assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&!dma[3]);
 usage[1]=0;assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&dma[3]);complete(&extra);
 // Repeated taps still carry a fresh down and up each time.
 for(unsigned i=0;i<4;i++){
  usage[1]=i%2?0:0xE9;
  assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&dma[3]);complete(&extra);
 }
 output_report[0]=0;output_report[1]=2;output_length=2;report_event(&nkro,APP_USBD_HID_USER_EVT_OUT_REPORT_READY);
 assert(nrf5_usb_driver()->keyboard_leds()==2);
 uint8_t led_diag[3];nrf5_esb_led_diagnostics(led_diag);assert(led_diag[0]==2&&led_diag[1]==0&&led_diag[2]==1);
 core_state=1;assert(!nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));core_state=3;
 submit_error=1;assert(!nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));assert(clears==1&&!dma[0]);submit_error=0;
 // Queue overrun drops only the newest request, without replacing an IN buffer.
 for(int i=0;i<5;i++)request(0);
 assert(request_count==4);int before=queries;
 for(int i=0;i<4;i++){nrf5_dongle_usb_task();uint8_t saved[32];memcpy(saved,dma[4],32);nrf5_dongle_usb_task();assert(!memcmp(saved,dma[4],32));complete(&control);}
 assert(queries==before+4&&request_count==0);

 // VIA responses are correlated by the wireless token, and C7 commands retain
 // their complete payload. Local diagnostics/DFU remain independent.
 rf_connected=true;midi_panic=0;
 memset(output_report,0,33);output_report[0]=0x81;output_report[32]=0xA5;output_length=33;
 control_event(&control,APP_USBD_HID_USER_EVT_OUT_REPORT_READY);nrf5_dongle_usb_task();
 assert(remote_pending==1&&!dma[4]&&forward[0]==0x81);
 uint8_t via_reply[32]={0x81,1,5,14};
 assert(nrf5_bridge_up_deliver(NRF5_BRIDGE_VIA,remote_token+1,via_reply,32)&&remote_pending==1);
 assert(nrf5_bridge_up_deliver(NRF5_BRIDGE_VIA,remote_token,via_reply,32));
 nrf5_dongle_usb_task();assert(dma[4]&&dma[4][2]==5);complete(&control);
 memset(output_report,0,33);output_report[0]=0xC7;output_report[1]=1;output_report[3]=3;
 for(unsigned i=4;i<20;i++)output_report[i]=i+1;
 output_report[32]=0xA5;
 control_event(&control,APP_USBD_HID_USER_EVT_OUT_REPORT_READY);nrf5_dongle_usb_task();
 assert(!dma[4]&&forward_kind==NRF5_ESB_DOWN_MUSIC&&forward_length==20&&forward_latest&&forward[19]==20);
 memset(output_report,0,33);output_report[1]=0xC7;output_report[2]=4;output_report[3]=29;
 control_event(&control,APP_USBD_HID_USER_EVT_OUT_REPORT_READY);nrf5_dongle_usb_task();assert(remote_pending==2&&!dma[4]);
 uint8_t control_reply[20]={0xC7,0x83,29,3,0,2,31,7};
 assert(nrf5_bridge_up_deliver(NRF5_BRIDGE_CONTROL,0,control_reply,20));
 nrf5_dongle_usb_task();assert(dma[4]&&dma[4][2]==29&&dma[4][3]==3);complete(&control);
 uint8_t note[4]={9,0x90,60,100};midi_block=true;
 assert(!nrf5_bridge_up_deliver(NRF5_BRIDGE_MIDI,0,note,4));midi_block=false;
 assert(nrf5_bridge_up_deliver(NRF5_BRIDGE_MIDI,0,note,4)&&!memcmp(midi_sent,note,4));
 nrf_midi_receive(note);assert(forward_kind==NRF5_ESB_DOWN_MIDI&&forward_length==4&&!forward_latest);
 memset(output_report,0,33);output_report[1]=0x81;
 control_event(&control,APP_USBD_HID_USER_EVT_OUT_REPORT_READY);nrf5_dongle_usb_task();assert(remote_pending);
 now+=3001;nrf5_dongle_usb_task();assert(dma[4]&&dma[4][0]==0xFF&&!remote_pending);complete(&control);
 // A transient submit failure keeps the accepted response and DFU intent.
 request(7);submit_error=1;nrf5_dongle_usb_task();assert(reply_ready&&!dfu_armed&&!dma[4]);
 submit_error=0;nrf5_dongle_usb_task();assert(dma[4]&&dfu_armed);now+=500;nrf5_dongle_usb_task();assert(!jumps);
 complete(&control);nrf5_dongle_usb_task();assert(jumps==1);
 usb_event(APP_USBD_EVT_DRV_RESET);assert(!dfu_armed&&!request_count);
 nrf5_dongle_usb_task();assert(windows==1); // bus reset is not physical insertion
 // Pair/save in progress or malformed reserved bytes must never reset.
 pairing_busy=true;request(7);nrf5_dongle_usb_task();assert(dma[4][2]==3&&!dfu_armed);complete(&control);pairing_busy=false;
 flash_pending=true;request(7);nrf5_dongle_usb_task();assert(dma[4][2]==3&&!dfu_armed);complete(&control);flash_pending=false;
 request(7);requests[request_head][10]=1;nrf5_dongle_usb_task();assert(dma[4][2]&&!dfu_armed);complete(&control);
 request(7);nrf5_dongle_usb_task();assert(dfu_armed);complete(&control);
 usb_event(APP_USBD_EVT_DRV_RESET);now+=1000;nrf5_dongle_usb_task();assert(jumps==1);
 usb_event(APP_USBD_EVT_DRV_SUSPEND);assert(suspended);
 usb_event(APP_USBD_EVT_POWER_REMOVED);usb_event(APP_USBD_EVT_STOPPED);assert(!power_enabled&&!started);
 core_state=1;usb_event(APP_USBD_EVT_POWER_DETECTED);usb_event(APP_USBD_EVT_POWER_READY);nrf5_dongle_usb_task();assert(windows==1);
 usb_event(APP_USBD_EVT_DRV_RESET);core_state=3;nrf5_dongle_usb_task();assert(windows==2);
 // A fresh USB host gets an initial released state even after prior deduplication.
 memset(motion,0,5);assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&dma[2]);complete(&mouse);
 assert(nrf5_usb_esb_report(NRF5_ESB_MOUSE,motion,5)&&!dma[2]);
 usage[0]=REPORT_ID_CONSUMER;usage[1]=usage[2]=0;
 assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&dma[3]);complete(&extra);
 assert(nrf5_usb_esb_report(NRF5_ESB_EXTRA,usage,3)&&!dma[3]);
}
''', {'nrf5_usb.h': USB, 'host_driver.h': helpers.HOST,
      'bootloader.h':'#pragma once\nvoid bootloader_jump(void);',
      'timer.h':'#pragma once\n#include <stdint.h>\nuint32_t timer_read32(void);\nuint32_t timer_elapsed32(uint32_t);'}, flags=['-DNRF5_ESB_DONGLE'])

if __name__ == '__main__': unittest.main()
