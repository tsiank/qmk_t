# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the actual ESB codec, bridge and timeslot state machine under ASan/UBSan.

The radio tests simulate peripheral events; they do not establish RF timing or
replace the two-board USB/RF hardware test.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PLATFORM = ROOT / 'platforms/nrf5'

HOST = r'''#pragma once
#include <stdint.h>
#include <stdbool.h>
#define REPORT_ID_SYSTEM 3
#define REPORT_ID_CONSUMER 4
typedef struct { uint8_t mods,reserved,keys[6]; } report_keyboard_t;
typedef struct { uint8_t report_id,mods,bits[30]; } report_nkro_t;
typedef struct { uint8_t report_id; uint16_t usage; } report_extra_t;
typedef struct { uint8_t buttons; int8_t x,y,v,h; } report_mouse_t;
typedef struct {
 uint8_t (*keyboard_leds)(void);
 void (*send_keyboard)(report_keyboard_t *);
 void (*send_nkro)(report_nkro_t *);
 void (*send_mouse)(report_mouse_t *);
 void (*send_extra)(report_extra_t *);
#ifdef RAW_ENABLE
 void (*send_raw_hid)(uint8_t *,uint8_t);
#endif
} host_driver_t;
'''

class EsbTests(unittest.TestCase):
    def test_pair_control_requires_usb_correlates_requests_and_never_returns_key(self):
        headers=self.transport_headers()
        headers.update({
            'action.h':'#pragma once\n#include <stdbool.h>\ntypedef struct {struct {bool pressed;} event;} keyrecord_t;',
            'nrf.h':'#pragma once\n#include <stdint.h>\nstatic struct {uint32_t DEVICEID[2];} ficr={{0x12345678,0xABCDEF01}};\n#define NRF_FICR (&ficr)',
        })
        self.run_c(r'''
#include <assert.h>
#include "esb_control.c"

bool nrf5_pair_busy(void){return false;}
bool nrf5_pair_task(uint32_t now){(void)now;return false;}
void nrf5_pair_receive(const uint8_t *p,uint8_t n){(void)p;(void)n;}
void nrf5_pair_poll_key(uint32_t now){(void)now;}
void nrf5_pair_key(bool p){(void)p;}
bool nrf5_pair_cancel(void){return true;}
bool nrf5_pair_start(void){return true;}
bool nrf5_pair_confirm(void){return true;}
uint8_t nrf5_pair_state(void){return 0;}
uint8_t nrf5_pair_seconds(void){return 0;}
uint32_t nrf5_pair_fingerprint(void){return 0;}

static nrf5_esb_settings_t config;
static int paired,cleared;
const nrf5_esb_settings_t *nrf5_esb_settings(void){return &config;}
bool nrf5_esb_settings_pair(const uint8_t k[16],uint32_t a){paired++;memcpy(config.key,k,16);config.address=a;config.paired=1;return true;}
void nrf5_esb_settings_clear(void){cleared++;config.paired=0;}
bool nrf5_esb_settings_pending(void){return false;}
uint32_t nrf5_esb_settings_error(void){return 0;}
bool nrf5_output_request(uint8_t m){if(m>4)return false;config.mode=m;return true;}
uint8_t nrf5_output_mode(void){return 1;}
bool nrf5_esb_connected(void){return false;}
static void request(uint8_t p[32],uint8_t op){memset(p,0,32);p[0]=0x84;p[1]=op;p[2]=1;p[30]=0x12;p[31]=0x34;}
int main(void){
 uint8_t p[32];request(p,2);memset(p+4,0xA5,16);p[23]=17;
 nrf5_esb_command(p,false);assert(p[2]==2&&!paired);
 request(p,2);memset(p+4,0xA5,16);p[23]=17;nrf5_esb_command(p,true);
 assert(paired==1&&p[2]==0&&p[6]==1&&p[25]==0x12&&p[26]==0x34);
 for(int i=0;i<32;i++)assert(p[i]!=0xA5);
 assert(config.key[0]==0xA5&&config.address==17);
 request(p,2);memset(p+4,0xA5,16);p[23]=17;p[24]=1;nrf5_esb_command(p,true);
 assert(p[2]==1&&paired==1);
 request(p,1);p[3]=3;nrf5_esb_command(p,true);assert(config.mode==3&&p[4]==3);
 request(p,3);nrf5_esb_command(p,false);assert(p[2]==2&&!cleared);
 request(p,3);nrf5_esb_command(p,true);assert(!p[6]&&cleared==1);
}
''',headers,['-I',str(ROOT/'tmk_core/protocol/nrf5'),'-DNRF5_ESB_ENABLE','-DNRF5_ESB_TX'])

    def test_pair_storage_is_separate_and_keeps_inflight_flash_data_immutable(self):
        self.run_c(r'''
#include <assert.h>
#include "esb_settings.c"
static const nrf5_esb_settings_t *flash_pointer;
static bool fail_write;
uint8_t eeprom_read_byte(const uint8_t *p){assert(!p);return 0;}
uint32_t fds_register(void (*fn)(const fds_evt_t *)){(void)fn;return 0;}
uint32_t fds_record_find(uint16_t f,uint16_t k,fds_record_desc_t *d,fds_find_token_t *t){(void)d;(void)t;assert(f==0x1002&&k==0x1001);return FDS_ERR_NOT_FOUND;}
uint32_t fds_record_open(fds_record_desc_t *d,fds_flash_record_t *r){(void)d;(void)r;return 99;}
uint32_t fds_record_close(fds_record_desc_t *d){(void)d;return 0;}
uint32_t fds_record_write(fds_record_desc_t *d,const fds_record_t *r){(void)d;assert(r->file_id==0x1002&&r->data.length_words==8);flash_pointer=r->data.p_data;return fail_write?FDS_ERR_BUSY:0;}
uint32_t fds_record_update(fds_record_desc_t *d,const fds_record_t *r){return fds_record_write(d,r);}
uint32_t fds_gc(void){return 0;}
static void complete(void){fds_evt_t e={.id=FDS_EVT_WRITE,.write={.file_id=0x1002,.record_key=0x1001}};event(&e);}
int main(void){
 nrf5_esb_settings_init();assert(!settings.paired);
 uint8_t a[16]={1},b[16]={2},zero[16]={0};
 assert(!nrf5_esb_settings_pair(zero,17));assert(nrf5_esb_settings_pair(a,17));
 nrf5_esb_settings_task();assert(busy&&flash_pointer->key[0]==1);
 assert(nrf5_esb_settings_pair(b,19));nrf5_esb_settings_task();assert(flash_pointer->key[0]==1);
 complete();assert(nrf5_esb_settings_pending());nrf5_esb_settings_task();assert(flash_pointer->key[0]==2);
 complete();assert(!nrf5_esb_settings_pending());assert(settings.crc==checksum(&settings));
 assert(nrf5_esb_settings_mode(NRF5_MODE_ESB));nrf5_esb_settings_clear();
 assert(!settings.paired&&settings.mode==NRF5_MODE_AUTO&&settings.key[0]==0);
 fail_write=true;nrf5_esb_settings_task();assert(nrf5_esb_settings_pending()&&nrf5_esb_settings_error()==FDS_ERR_BUSY);
 fail_write=false;nrf5_esb_settings_task();complete();assert(!nrf5_esb_settings_pending()&&!nrf5_esb_settings_error());
}
''', {'fds.h': r'''#pragma once
#include <stdint.h>
#define FDS_ERR_NOT_FOUND 1
#define FDS_ERR_BUSY 2
#define FDS_ERR_NO_SPACE_IN_FLASH 3
#define FDS_EVT_WRITE 1
#define FDS_EVT_UPDATE 2
#define FDS_EVT_GC 3
typedef struct {uint32_t a;} fds_record_desc_t;
typedef struct {uint32_t a;} fds_find_token_t;
typedef struct {uint16_t file_id,key;struct {const void *p_data;uint16_t length_words;} data;} fds_record_t;
typedef struct {const struct {uint16_t length_words;} *p_header;const void *p_data;} fds_flash_record_t;
typedef struct {uint32_t id,result;struct {uint16_t file_id,record_key;} write;} fds_evt_t;
uint32_t fds_register(void (*)(const fds_evt_t *));
uint32_t fds_record_find(uint16_t,uint16_t,fds_record_desc_t *,fds_find_token_t *);
uint32_t fds_record_open(fds_record_desc_t *,fds_flash_record_t *);
uint32_t fds_record_close(fds_record_desc_t *);
uint32_t fds_record_write(fds_record_desc_t *,const fds_record_t *);
uint32_t fds_record_update(fds_record_desc_t *,const fds_record_t *);
uint32_t fds_gc(void);
'''}, flags=['-DEEPROM_TEST_HARNESS'])

    def test_manual_mode_is_not_overridden_by_usb_and_releases_old_host(self):
        headers = self.transport_headers()
        headers['nrf5_usb.h'] += '\nextern uint8_t keyboard_protocol;'
        headers.update({
            'action.h': '#pragma once\n#include <stdbool.h>\ntypedef struct {struct {bool pressed;} event;} keyrecord_t;',
            'host.h': '#pragma once\n#include "host_driver.h"\nhost_driver_t *host_get_driver(void);\nvoid host_set_driver(host_driver_t *);',
            'action_util.h': 'void clear_keyboard(void);',
            'ble_service.h': 'void advertising_start(bool);\nvoid ble_disconnect(void);',
            'ble_hids_service.h': '#include "host_driver.h"\nhost_driver_t *nrf5_ble_driver(void);\nbool nrf5_ble_can_send_nkro(void);',
            'keycodes.h': '#define KC_ESCAPE 41\n#define QK_OUTPUT_USB 1\n#define QK_OUTPUT_BLUETOOTH 2\n#define QK_OUTPUT_NONE 3\n#define QK_OUTPUT_AUTO 4\n#define QK_OUTPUT_2P4GHZ 5\n#define QK_OUTPUT_NEXT 6\n#define QK_OUTPUT_PREV 7',
            'nrf.h': '#pragma once\n#include <stdint.h>\nstatic struct {uint32_t USBREGSTATUS;} power;\nstatic struct {uint32_t ENABLE;} usb;\n#define NRF_POWER (&power)\n#define NRF_USBD (&usb)\n#define POWER_USBREGSTATUS_VBUSDETECT_Msk 1',
        })
        self.run_c(r'''
#include <assert.h>
#include "outputselect.c"

bool nrf5_pair_busy(void){return false;}
bool nrf5_pair_task(uint32_t now){(void)now;return false;}
void nrf5_pair_receive(const uint8_t *p,uint8_t n){(void)p;(void)n;}
void nrf5_pair_poll_key(uint32_t now){(void)now;}
void nrf5_pair_key(bool p){(void)p;}
bool nrf5_pair_cancel(void){return true;}
bool nrf5_pair_start(void){return true;}
bool nrf5_pair_confirm(void){return true;}
uint8_t nrf5_pair_state(void){return 0;}
uint8_t nrf5_pair_seconds(void){return 0;}
uint32_t nrf5_pair_fingerprint(void){return 0;}

static host_driver_t usb_driver,ble_driver,rf_driver,quiet_driver,*selected_driver;
static nrf5_esb_settings_t config={.paired=1,.mode=NRF5_MODE_USB};
static uint32_t milliseconds;
static int cleared,advertised,disconnected,releases;
static bool rf_active,rf_idle=true,rf_present,rf_probe;
uint8_t keyboard_protocol=1;
uint32_t timer_read32(void){return milliseconds;}
uint32_t nrf5_esb_now(void){return milliseconds;}
host_driver_t *host_get_driver(void){return selected_driver;}
void host_set_driver(host_driver_t *d){selected_driver=d;}
host_driver_t *nrf5_usb_driver(void){return &usb_driver;}
host_driver_t *nrf5_ble_driver(void){return &ble_driver;}
host_driver_t *nrf5_esb_driver(void){return &rf_driver;}
host_driver_t *nrf5_esb_quiet_driver(void){return &quiet_driver;}
bool nrf5_ble_can_send_nkro(void){return true;}
void clear_keyboard(void){cleared++;}
void ble_disconnect(void){disconnected++;}
void advertising_start(bool b){assert(!b&&nrf5_output_ble_allowed());advertised++;}
void nrf5_esb_set_active(bool b){rf_active=b;if(!rf_probe)rf_present=false;}
void nrf5_esb_set_probe(bool b){rf_probe=b;}
bool nrf5_esb_connected(void){return rf_present;}
bool nrf5_esb_idle(void){return rf_idle;}
void nrf5_esb_release(void){releases++;}
const nrf5_esb_settings_t *nrf5_esb_settings(void){return &config;}
bool nrf5_esb_settings_mode(uint8_t mode){config.mode=mode;return true;}
static void choose(uint8_t mode){assert(nrf5_output_request(mode));milliseconds+=201;nrf5_output_task();}
int main(void){
 power.USBREGSTATUS=1;nrf5_output_init();assert(selected_driver==&usb_driver);
 choose(NRF5_MODE_BLE);assert(selected_driver==&ble_driver&&advertised==1);
 set_output(OUTPUT_USB);nrf5_output_task();assert(selected_driver==&ble_driver);
 keyrecord_t record={.event={.pressed=true}};
 process_nrf5_output(QK_OUTPUT_2P4GHZ,&record);milliseconds+=201;nrf5_output_task();
 assert(selected_driver==&rf_driver&&rf_active&&disconnected==1&&host_can_send_nkro());
 set_output(OUTPUT_USB);nrf5_output_task();assert(selected_driver==&rf_driver);
 rf_idle=false;choose(NRF5_MODE_USB);assert(selected_driver==&quiet_driver&&releases==1);
 int before=cleared;nrf5_output_task();assert(cleared==before);
 rf_idle=true;nrf5_output_task();assert(selected_driver==&usb_driver&&!rf_active);
 choose(NRF5_MODE_AUTO);assert(selected_driver==&usb_driver);
 power.USBREGSTATUS=0;nrf5_output_task();assert(selected_driver==&ble_driver&&rf_probe);
 rf_present=true;nrf5_output_task();assert(selected_driver==&rf_driver&&rf_active&&rf_present);
 rf_present=false;rf_idle=false;nrf5_output_task();assert(selected_driver==&quiet_driver);
 milliseconds+=101;nrf5_output_task();assert(selected_driver==&ble_driver&&rf_probe&&!rf_active&&advertised==3);
 int adv_before=advertised;for(int i=0;i<20;i++){milliseconds+=10;nrf5_output_task();}
 assert(advertised==adv_before); // no repeated advertising or USB/BLE oscillation
 choose(NRF5_MODE_ESB);assert(selected_driver==&rf_driver&&!rf_probe);
 milliseconds+=2000;nrf5_output_task();assert(selected_driver==&rf_driver); // manual stays manual
 rf_idle=true;choose(NRF5_MODE_BLE);assert(selected_driver==&ble_driver&&!rf_probe);
 power.USBREGSTATUS=1;nrf5_output_task();assert(selected_driver==&ble_driver);
 choose(NRF5_MODE_AUTO);assert(selected_driver==&usb_driver&&!rf_probe);
}
''', headers, ['-I', str(ROOT/'tmk_core/protocol/nrf5'), '-DNRF5_ESB_ENABLE', '-DNRF5_ESB_TX', '-DNKRO_ENABLE'])

    def run_c(self, source, headers=None, flags=()):
        if not shutil.which('cc'):
            self.skipTest('Native C compiler required')
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            for name, data in (headers or {}).items():
                (work / name).write_text(data)
            (work / 'test.c').write_text(source)
            result = subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-I', directory,
                '-I', str(PLATFORM), *flags, str(work / 'test.c'), str(PLATFORM / 'esb_packet.c'),
                '-o', str(work / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(work / 'test')], capture_output=True, text=True,
                env={**os.environ, 'ASAN_OPTIONS': 'detect_stack_use_after_return=1'})
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def radio_headers(self):
        return {
            'nrf.h': r'''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {
 uint32_t POWER,MODE,TXPOWER,FREQUENCY,PCNF0,PCNF1,BASE0,BASE1,PREFIX0,TXADDRESS,RXADDRESSES,RXMATCH;
 uint32_t CRCCNF,CRCINIT,CRCPOLY,SHORTS,INTENCLR,INTENSET,PACKETPTR;
 uint32_t EVENTS_END,EVENTS_DISABLED,TASKS_RXEN,TASKS_TXEN,TASKS_DISABLE,STATE,CRCSTATUS;
} fake_radio_t;
typedef struct {uint32_t TASKS_CAPTURE[4],CC[4],EVENTS_COMPARE[4],INTENCLR,INTENSET;} fake_timer_t;
static fake_radio_t peripheral_radio;
static fake_timer_t peripheral_timer;
#define NRF_RADIO (&peripheral_radio)
#define NRF_TIMER0 (&peripheral_timer)
#define RADIO_MODE_MODE_Nrf_2Mbit 1
#define RADIO_TXPOWER_TXPOWER_0dBm 0
#define RADIO_PCNF0_LFLEN_Pos 0
#define RADIO_PCNF0_S1LEN_Pos 16
#define RADIO_PCNF1_ENDIAN_Big 1
#define RADIO_PCNF1_ENDIAN_Pos 24
#define RADIO_PCNF1_BALEN_Pos 16
#define RADIO_CRCCNF_LEN_Two 2
#define RADIO_SHORTS_READY_START_Msk 1
#define RADIO_SHORTS_END_DISABLE_Msk 2
#define RADIO_SHORTS_DISABLED_RXEN_Msk 4
#define RADIO_INTENSET_DISABLED_Msk 16
#define RADIO_STATE_STATE_Disabled 0
#define TIMER_INTENSET_COMPARE0_Msk 1
#define TIMER_INTENSET_COMPARE1_Msk 2
#define RADIO_IRQn 0
#define TIMER0_IRQn 1
static int irq_enabled[2];
static void NVIC_EnableIRQ(int n){irq_enabled[n]=1;}
static void NVIC_DisableIRQ(int n){irq_enabled[n]=0;}
static void NVIC_ClearPendingIRQ(int n){(void)n;}
#define __DMB() __asm__ volatile("" ::: "memory")
''',
            'nrf_soc.h': r'''#pragma once
#define NRF_SUCCESS 0
#define NRF_RADIO_REQ_TYPE_EARLIEST 1
#define NRF_RADIO_HFCLK_CFG_XTAL_GUARANTEED 1
#define NRF_RADIO_PRIORITY_NORMAL 1
enum {NRF_RADIO_CALLBACK_SIGNAL_TYPE_START,NRF_RADIO_CALLBACK_SIGNAL_TYPE_TIMER0,NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO};
enum {NRF_RADIO_SIGNAL_CALLBACK_ACTION_NONE,NRF_RADIO_SIGNAL_CALLBACK_ACTION_END,NRF_RADIO_SIGNAL_CALLBACK_ACTION_REQUEST_AND_END};
enum {NRF_EVT_RADIO_SESSION_IDLE,NRF_EVT_RADIO_BLOCKED,NRF_EVT_RADIO_CANCELED,NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN,NRF_EVT_RADIO_SESSION_CLOSED};
typedef struct {int request_type;union {struct {int hfclk,priority,length_us,timeout_us;} earliest;} params;} nrf_radio_request_t;
typedef struct {int callback_action;union {struct {nrf_radio_request_t *p_next;} request;} params;} nrf_radio_signal_callback_return_param_t;
static int requests;
static uint32_t sd_radio_session_open(nrf_radio_signal_callback_return_param_t *(*fn)(uint8_t)){(void)fn;return 0;}
static uint32_t sd_radio_request(nrf_radio_request_t *r){assert(r->params.earliest.length_us==8000);requests++;return 0;}
''',
            'nrf_sdh_soc.h': '#pragma once\n#define NRF_SDH_SOC_OBSERVER(name,prio,fn,arg) static void *name __attribute__((used)) = (void *)fn;',
            'app_error.h': '#pragma once\n#include <assert.h>\n#define APP_ERROR_CHECK(rc) assert((rc)==0)',
        }

    def test_timeslot_sender_ack_retry_and_cleanup(self):
        self.run_c(r'''
#include <assert.h>
#include "esb_radio.c"
static void event(void){NRF_RADIO->EVENTS_DISABLED=NRF_RADIO->EVENTS_END=1;signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO);}
int main(void){
 nrf5_esb_radio_init();nrf5_esb_radio_configure(true,17);assert(requests==0);
 uint8_t p[64];nrf5_esb_report_t r={.kind=NRF5_ESB_KEYBOARD,.length=8,.data={0,0,4}};
 uint8_t n=nrf5_esb_encode(p,0x12345678,19,&r);
 assert(!nrf5_esb_radio_send(p,0));assert(nrf5_esb_radio_send(p,n));
 assert(!nrf5_esb_radio_send(p,n));assert(requests==1);
 signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_START);
 assert(irq_enabled[0]&&irq_enabled[1]&&state==RF_SEND_DATA);
 assert((NRF_RADIO->SHORTS&RADIO_SHORTS_DISABLED_RXEN_Msk)!=0);
 // Hardware must begin RX at DISABLED, before the application callback runs.
 assert(NRF_RADIO->TASKS_RXEN==0);
 assert(NRF_TIMER0->CC[0]<SLOT_US && NRF_RADIO->PCNF0==(8|(3<<16)));
 NRF_TIMER0->CC[2]=300;event();assert(state==RF_WAIT_ACK);
 assert((NRF_RADIO->SHORTS&RADIO_SHORTS_DISABLED_RXEN_Msk)==0);
 assert(NRF_RADIO->PACKETPTR==(uint32_t)(uintptr_t)&rx&&NRF_RADIO->TASKS_RXEN==0);
 assert(NRF_TIMER0->CC[1]==1000);
 // No ACK -> stop RX and retransmit, without calling any SVC at priority 0.
 NRF_TIMER0->CC[2]=1000;NRF_TIMER0->EVENTS_COMPARE[1]=1;
 signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_TIMER0);
 assert(state==RF_SEND_DATA && requests==1);
 assert((NRF_RADIO->SHORTS&RADIO_SHORTS_DISABLED_RXEN_Msk)!=0);
 NRF_TIMER0->CC[2]=1300;event();assert(state==RF_WAIT_ACK);
 rx.length=NRF5_ESB_ACK_SIZE;rx.s1=tx.s1;rx.data[0]=0xA2;
 memcpy(rx.data+1,tx.data+4,12);rx.data[13]=2;
 NRF_RADIO->CRCSTATUS=1;NRF_TIMER0->CC[2]=1450;
 rx.s1=tx.s1^2;event();assert(!tx_acked&&state==RF_WAIT_ACK);
 uint8_t diagnostic[32]={0x87};nrf5_esb_radio_diagnostics(diagnostic);
 assert(diagnostic[0]==0x87&&diagnostic[1]==1&&diagnostic[2]==1&&diagnostic[3]==2);
 assert(diagnostic[19]==1&&diagnostic[28]==30&&diagnostic[29]==(tx.s1^2)&&diagnostic[31]==tx.s1);
 rx.s1=tx.s1;NRF_TIMER0->CC[2]=1500;event();
 assert(tx_acked&&state==RF_IDLE);uint8_t reply[30];nrf5_esb_radio_ack(reply);assert(reply[13]==2);
 assert(action.callback_action==NRF_RADIO_SIGNAL_CALLBACK_ACTION_END);
 assert(!irq_enabled[0]&&!irq_enabled[1]);
 bool ok;assert(!nrf5_esb_radio_result(&ok));
 soc_event(NRF_EVT_RADIO_SESSION_IDLE,0);
 assert(nrf5_esb_radio_result(&ok)&&ok);
 assert(nrf5_esb_radio_send(p,n));
 soc_event(NRF_EVT_RADIO_BLOCKED,0);
 assert(nrf5_esb_radio_result(&ok)&&!ok);
 uint32_t stats[5];nrf5_esb_radio_counters(stats);
 assert(stats[0]==1&&stats[1]==2&&stats[2]==1&&stats[4]==1);
}
''', self.radio_headers(), ['-DNRF5_ESB_TX'])

    def test_timeslot_receiver_ack_backpressure_and_grant_boundary(self):
        self.run_c(r'''
#include <assert.h>
#include "esb_radio.c"
static void event(void){NRF_RADIO->EVENTS_DISABLED=NRF_RADIO->EVENTS_END=1;signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO);}
int main(void){
 nrf5_esb_radio_init();nrf5_esb_radio_configure(true,17);nrf5_esb_radio_task();assert(requests==1);
 signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_START);
 nrf5_esb_report_t r={.kind=NRF5_ESB_KEYBOARD,.length=8,.data={0,0,4}};
 rx.length=nrf5_esb_encode(rx.data,17,22,&r);rx.s1=5;
 NRF_RADIO->CRCSTATUS=1;NRF_TIMER0->CC[2]=300;
 event();assert(state==RF_LISTEN); // first packet awaits thread-mode authentication
 uint8_t p[64],n;assert(nrf5_esb_radio_receive(p,&n)&&n==32&&p[18]==4);
 uint8_t response[30]={0xA2};response[13]=2;
 nrf5_esb_radio_cache_ack(p,n,response);event();
 assert(state==RF_SEND_ACK && ack.length==30 && ack.s1==5 && ack.data[13]==2);
 event();assert(state==RF_LISTEN);
 uint8_t diagnostic[32]={0x87};nrf5_esb_radio_diagnostics(diagnostic);
 assert(diagnostic[0]==0x87&&diagnostic[1]==1&&diagnostic[2]==2);
 assert(diagnostic[7]==1&&diagnostic[11]==1&&diagnostic[23]==1&&diagnostic[27]==1);
 // Fill the ISR -> main FIFO. Do not ACK a packet that cannot be retained.
 cache_ready=false;for(int i=0;i<NRF5_ESB_QUEUE_SIZE-1;i++){event();assert(state==RF_LISTEN);}
 event();assert(state==RF_LISTEN);
 assert(requests==1); // no SVC from the callback
 // A frame at the cleanup boundary must not start a new ACK transmission.
 NRF_TIMER0->CC[2]=STOP_US-100;event();assert(state==RF_LISTEN);
 NRF_TIMER0->EVENTS_COMPARE[0]=1;
 nrf_radio_signal_callback_return_param_t *a=signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_TIMER0);
 assert(a->callback_action==NRF_RADIO_SIGNAL_CALLBACK_ACTION_REQUEST_AND_END);
 assert(a->params.request.p_next==&request);
 assert(!irq_enabled[0]&&!irq_enabled[1]&&state==RF_IDLE);
 soc_event(NRF_EVT_RADIO_CANCELED,0);nrf5_esb_radio_task();assert(requests==2);
 soc_event(NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN,0);
 nrf5_esb_radio_task();assert(nrf5_esb_radio_error()==NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN);
}
''', self.radio_headers(), ['-DNRF5_ESB_RX'])

    def test_pairing_offer_uses_full_payload_and_replies_on_discovery_pipe(self):
        self.run_c(r'''
#include <assert.h>
#include "esb_radio.c"
static void event(void){NRF_RADIO->EVENTS_DISABLED=NRF_RADIO->EVENTS_END=1;signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO);}
int main(void){
 nrf5_esb_radio_init();nrf5_esb_radio_configure(true,17);nrf5_esb_radio_listen_pair(true);
 nrf5_esb_radio_task();signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_START);
 assert(NRF_RADIO->BASE1==NRF5_ESB_PAIR_ADDRESS&&NRF_RADIO->RXADDRESSES==3);
 rx.length=56;rx.s1=3;memset(rx.data,0,64);rx.data[0]=0x51;rx.data[1]=3;rx.data[2]=7;rx.data[3]=32;rx.data[4]=1;rx.data[12]=1;
 NRF_RADIO->RXMATCH=1;NRF_RADIO->CRCSTATUS=1;NRF_TIMER0->CC[2]=300;event();
 uint8_t request[64],length;assert(nrf5_esb_radio_receive(request,&length)&&length==56);
 uint8_t offer[64]={0xA3,3,8,40};memcpy(offer+4,request+4,12);
 nrf5_esb_radio_cache_reply(request,length,offer,64);event();
 assert(state==RF_SEND_ACK&&ack.length==64&&NRF_RADIO->TXADDRESS==1);
 assert(!memcmp(ack.data,offer,64));event();
 nrf5_esb_radio_listen_pair(false);event();assert(state==RF_LISTEN);
 assert(!nrf5_esb_radio_receive(request,&length));
}
''', self.radio_headers(), ['-DNRF5_ESB_RX'])

    def test_pairing_reply_mailbox_does_not_become_a_normal_data_ack(self):
        self.run_c(r'''
#include <assert.h>
#include "esb_radio.c"
static void event(void){NRF_RADIO->EVENTS_DISABLED=NRF_RADIO->EVENTS_END=1;signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO);}
int main(void){
 nrf5_esb_radio_init();nrf5_esb_radio_configure(true,NRF5_ESB_PAIR_ADDRESS);
 uint8_t p[64]={0x51,3,7,32};p[4]=1;p[12]=1;
 assert(nrf5_esb_radio_send(p,56));signal_callback(NRF_RADIO_CALLBACK_SIGNAL_TYPE_START);
 NRF_TIMER0->CC[2]=300;event();rx.length=64;rx.s1=tx.s1;memset(rx.data,0,64);
 rx.data[0]=0xA3;rx.data[1]=3;rx.data[2]=8;rx.data[3]=40;memcpy(rx.data+4,p+4,12);
 NRF_RADIO->CRCSTATUS=1;NRF_TIMER0->CC[2]=600;event();soc_event(NRF_EVT_RADIO_SESSION_IDLE,0);
 bool ok;assert(nrf5_esb_radio_result(&ok)&&ok);uint8_t r[64];assert(nrf5_esb_radio_reply(r)==64&&r[0]==0xA3);
 memset(r,255,30);nrf5_esb_radio_ack(r);for(int i=0;i<30;i++)assert(!r[i]);
}
''', self.radio_headers(), ['-DNRF5_ESB_TX'])

    def test_wire_roundtrip_malformed_frames_and_sequence_wrap(self):
        self.run_c(r'''
#include <assert.h>
#include <string.h>
#include "esb_packet.h"
int main(void) {
 uint8_t p[64]; uint64_t boot; uint32_t seq;
 nrf5_esb_report_t report={.kind=NRF5_ESB_NKRO,.length=31}, copy;
 for(int i=0;i<31;i++)report.data[i]=i;
 uint8_t size=nrf5_esb_encode(p,0x80FFFE01,UINT32_MAX,&report);
 assert(size==55 && nrf5_esb_decode(p,size,&boot,&seq,&copy));
 assert(boot==0x80FFFE01 && seq==UINT32_MAX && !memcmp(copy.data,report.data,31));
 assert(nrf5_esb_newer(0,UINT32_MAX) && !nrf5_esb_newer(UINT32_MAX,0));
 assert(!nrf5_esb_newer(3,3) && !nrf5_esb_newer(0x80000000u,0));
 for(int i=0;i<size;i++)assert(!nrf5_esb_decode(p,i,&boot,&seq,&copy));
 assert(!nrf5_esb_decode(p,65,&boot,&seq,&copy));
 p[1]=3;assert(!nrf5_esb_decode(p,size,&boot,&seq,&copy));p[1]=2;
 p[3]=32;assert(!nrf5_esb_decode(p,size,&boot,&seq,&copy));p[3]=31;
 memset(p+4,0,8);assert(!nrf5_esb_decode(p,size,&boot,&seq,&copy));
 report.kind=NRF5_ESB_EXTRA;report.length=3;report.data[0]=7;
 assert(nrf5_esb_encode(p,1,1,&report)==0);
 for(int n=0;n<256;n++) { memset(p,n,sizeof(p)); nrf5_esb_decode(p,n%65,&boot,&seq,&copy); }
}
''')

    def transport_headers(self):
        return {'host_driver.h': HOST,
            'nrf5_usb.h': '#pragma once\n#include "host_driver.h"\nhost_driver_t *nrf5_usb_driver(void);\nbool nrf5_usb_esb_report(uint8_t,const uint8_t *,uint8_t);',
            'timer.h': '#pragma once\n#include <stdint.h>\nuint32_t timer_read32(void);',
            'nrf_soc.h': '#pragma once\n#include <stdint.h>\n#define NRF_SUCCESS 0\nuint32_t sd_rand_application_vector_get(uint8_t *,uint8_t);'}

    def test_actual_usb_bridge_owns_dma_buffer_and_retries_failed_submission(self):
        # Compile the production block in isolation; the remainder of nrf5_usb.c
        # needs the whole Nordic USB descriptor stack, covered by firmware builds.
        usb = (ROOT / 'tmk_core/protocol/nrf5/usb/nrf5_usb.c').read_text()
        start = usb.index('static const app_usbd_class_inst_t *esb_usb_active;')
        end = usb.index('\n#ifdef MUSIC_STREAM_ENABLE', start)
        block = usb[start:end].rsplit('#endif', 1)[0]
        self.run_c(r'''
#include <assert.h>
#include <string.h>
#include "esb_packet.h"
typedef struct {int id;} app_usbd_class_inst_t;
typedef app_usbd_class_inst_t app_usbd_hid_generic_t;
typedef int ret_code_t;
static const app_usbd_hid_generic_t m_app_hid_kbd={1},m_app_hid_kbd2={2},m_app_hid_mouse={3},m_app_hid_extra={4};
static struct {int ENABLE;} usbd={1};
#define NRF_USBD (&usbd)
#define NRF_SUCCESS 0
#define APP_USBD_STATE_Configured 3
#define NKRO_EPSIZE 22
#define UNUSED_RETURN_VALUE(x) (void)(x)
static int core_state=3,submit_error,cleared,submissions;
static const uint8_t *dma_pointer;
static const app_usbd_class_inst_t *dma_instance;
static int app_usbd_core_state_get(void){return core_state;}
static const app_usbd_class_inst_t *app_usbd_hid_generic_class_inst_get(const app_usbd_hid_generic_t *p){return p;}
static int app_usbd_hid_generic_in_report_set(const app_usbd_hid_generic_t *p,const void *data,unsigned size){
 assert(size<=32);dma_pointer=data;dma_instance=p;submissions++;return submit_error;
}
static int hid_generic_clear_buffer(const app_usbd_class_inst_t *p){assert(p==dma_instance);dma_pointer=0;cleared++;return 0;}
''' + block + r'''
int main(void){
 uint8_t data[32]={0,0,4};
 assert(nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));
 assert(dma_pointer!=data && dma_pointer[2]==4);
 data[2]=5;assert(!nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));assert(dma_pointer[2]==4);
 esb_usb_done(&m_app_hid_mouse);assert(!nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));
 esb_usb_done(&m_app_hid_kbd);submit_error=1;
 assert(!nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));assert(cleared==1 && !dma_pointer);
 submit_error=0;assert(nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));assert(dma_pointer[2]==5);
 esb_usb_done(&m_app_hid_kbd);core_state=1;
 assert(!nrf5_usb_esb_report(NRF5_ESB_KEYBOARD,data,8));core_state=3;
 assert(nrf5_usb_esb_report(NRF5_ESB_NKRO,data,31));assert(dma_instance==&m_app_hid_kbd2);
 esb_usb_done(&m_app_hid_kbd2);assert(submissions==4);
}
''', flags=['-DNKRO_ENABLE', '-DMOUSE_ENABLE', '-DEXTRAKEY_ENABLE'])

    def test_transmitter_retries_preserves_taps_mode_switch_and_recovers_overflow(self):
        self.run_c(r'''
#include <assert.h>
#include <string.h>
#include "esb_transport.c"

bool nrf5_pair_busy(void){return false;}
bool nrf5_pair_task(uint32_t now){(void)now;return false;}
void nrf5_pair_receive(const uint8_t *p,uint8_t n){(void)p;(void)n;}
void nrf5_pair_poll_key(uint32_t now){(void)now;}
void nrf5_pair_key(bool p){(void)p;}
bool nrf5_pair_cancel(void){return true;}
bool nrf5_pair_start(void){return true;}
bool nrf5_pair_confirm(void){return true;}
uint8_t nrf5_pair_state(void){return 0;}
uint8_t nrf5_pair_seconds(void){return 0;}
uint32_t nrf5_pair_fingerprint(void){return 0;}


static nrf5_esb_settings_t configuration={.paired=1,.address=17,.key={1}};
void nrf5_esb_settings_init(void){}
void nrf5_esb_settings_task(void){}
const nrf5_esb_settings_t *nrf5_esb_settings(void){return &configuration;}
bool nrf5_esb_settings_pending(void){return false;}
uint32_t nrf5_esb_settings_error(void){return 0;}
void nrf5_esb_radio_configure(bool active,uint32_t address){(void)active;(void)address;}
bool nrf5_esb_seal(const uint8_t k[16],uint64_t c,uint8_t *p,uint8_t n){(void)k;(void)c;(void)p;(void)n;return true;}
bool nrf5_esb_open(const uint8_t k[16],uint64_t c,uint8_t *p,uint8_t n){(void)k;(void)c;(void)p;(void)n;return true;}
void nrf5_esb_radio_ack(uint8_t ack[30]){memset(ack,0,30);}
bool nrf5_esb_ack_open(const uint8_t k[16],const uint8_t *p,const uint8_t a[30],uint64_t *c,uint8_t *l,bool *v){(void)k;(void)p;(void)a;*c=9;*l=2;*v=true;return true;}
bool nrf5_esb_ack_seal(const uint8_t k[16],const uint8_t *p,uint64_t c,uint8_t l,bool v,uint8_t a[30]){(void)k;(void)p;(void)c;(void)l;(void)v;memset(a,0,30);return true;}
void nrf5_esb_radio_cache_ack(const uint8_t *p,uint8_t n,const uint8_t a[30]){(void)p;(void)n;(void)a;}

static uint32_t clock_ms;
static bool power_idle;
bool nrf5_wireless_idle(void){return power_idle;}
static bool busy,done,acked;
static uint8_t sent[64],sent_length;
uint32_t timer_read32(void){return clock_ms;}
uint32_t sd_rand_application_vector_get(uint8_t *p,uint8_t n){memset(p,0xAB,n);return 0;}
void nrf5_esb_radio_init(void){}
void nrf5_esb_radio_task(void){}
uint32_t nrf5_esb_radio_error(void){return 0;}
void nrf5_esb_radio_counters(uint32_t p[5]){memset(p,0,5*sizeof(*p));}
bool nrf5_esb_radio_send(const uint8_t *p,uint8_t n){assert(!busy);memcpy(sent,p,n);sent_length=n;busy=true;return true;}
bool nrf5_esb_radio_result(bool *a){if(!done)return false;*a=acked;done=busy=false;return true;}
uint8_t nrf5_esb_radio_leds(void){return 2;}
static int via_replies;
void nrf_via_send(uint8_t *p,uint8_t n){assert(n==32 && p[0]==1);via_replies++;}
static void complete(bool ok){assert(busy);acked=ok;done=true;clock_ms++;nrf5_esb_task();}
static void drain(void){for(int i=0;i<100 && (count||sending);i++)complete(true);assert(!count&&!sending);}
int main(void){
 nrf5_esb_init();nrf5_esb_set_active(true);nrf5_esb_task();drain();
 assert(nrf5_esb_connected() && driver.keyboard_leds()==2);
 uint8_t via[32]={1};driver.send_raw_hid(via,32);assert(via_replies==1);
 via[0]=0x83;nrf5_esb_diagnostics(via);assert(via[0]==0x83&&via[1]==2&&via[2]==1&&via[3]==1);
 report_keyboard_t k={.mods=2,.keys={4}};
 driver.send_keyboard(&k);k.mods=0;k.keys[0]=0;driver.send_keyboard(&k);
 nrf5_esb_task();assert(sent[16]==2 && sent[18]==4);
 uint8_t first[64];memcpy(first,sent,sent_length);
 complete(false);assert(!memcmp(first,sent,sent_length));
 complete(true);assert(sent[16]==0 && sent[18]==0);drain();
 report_nkro_t nk={.mods=1,.bits={0x10}};
 driver.send_nkro(&nk);nrf5_esb_task();assert(sent[2]==NRF5_ESB_KEYBOARD);
 complete(true);assert(sent[2]==NRF5_ESB_NKRO && sent[16]==1);drain();
 k.keys[0]=5;driver.send_keyboard(&k);nrf5_esb_task();
 assert(sent[2]==NRF5_ESB_NKRO && sent[16]==0);complete(true);
 assert(sent[2]==NRF5_ESB_KEYBOARD && sent[18]==5);
 // Queue replacement while a DMA packet is outstanding must not dequeue
 // the new release/reset head when the old packet's ACK arrives.
 for(int i=0;i<80;i++){k.keys[0]=i%2?0:6;driver.send_keyboard(&k);}
 assert(count<=NRF5_ESB_QUEUE_SIZE);drain();
 assert(!keyboard[2] && !nkro[0]);
 clock_ms+=NRF5_ESB_TIMEOUT_MS+1;nrf5_esb_task();assert(!nrf5_esb_connected());
 drain();assert(nrf5_esb_connected());
 // Held mouse buttons are refreshed, relative movement is never replayed.
 report_mouse_t m={.buttons=1,.x=7};driver.send_mouse(&m);nrf5_esb_task();drain();
 snapshot_at=clock_ms;
 clock_ms+=41;nrf5_esb_task();
 assert(sent[2]==NRF5_ESB_HEARTBEAT && count==1);drain();
 // Heartbeats keep the link alive until the less frequent full snapshot.
 while(clock_ms-snapshot_at<NRF5_ESB_SNAPSHOT_MS-41) {
  clock_ms+=41;nrf5_esb_task();assert(sent[2]==NRF5_ESB_HEARTBEAT);drain();
  assert(nrf5_esb_connected());
 }
 clock_ms+=41;nrf5_esb_task();
 for(int i=0;i<4;i++)complete(true);
 assert(sent[2]==NRF5_ESB_MOUSE && sent[16]==1 && sent[17]==0);drain();
 // Idle heartbeats slow down, but physical changes never wait for that timer.
 power_idle=true;refresh_at=snapshot_at=clock_ms;
 assert(nrf5_esb_next_wake_ticks()==100);
 clock_ms+=37;assert(nrf5_esb_next_wake_ticks()==63);clock_ms-=37;
 clock_ms+=NRF5_ESB_IDLE_REFRESH_MS-1;nrf5_esb_task();assert(!busy);
 assert(nrf5_esb_next_wake_ticks()==1);
 clock_ms++;nrf5_esb_task();assert(busy && sent[2]==NRF5_ESB_HEARTBEAT);drain();
 assert(nrf5_esb_next_wake_ticks()<=100);
 // A queued key or pending RF result retains short service, even in idle.
 k.keys[0]=8;driver.send_keyboard(&k);assert(nrf5_esb_next_wake_ticks()==40);
 nrf5_esb_task();assert(busy&&nrf5_esb_next_wake_ticks()==40);drain();
 // An approaching supervision deadline wins over the next heartbeat.
 uint32_t previous_activity=last_activity;
 refresh_at=clock_ms;last_activity=clock_ms-290;
 assert(nrf5_esb_next_wake_ticks()==10);last_activity=previous_activity;
 // The extended RTC clock survives a raw counter wrap.
 clock_ms=0x7ffff-20;oldest_at=refresh_at=last_activity=nrf5_esb_now();
 assert(nrf5_esb_next_wake_ticks()==100);clock_ms+=50;
 assert(nrf5_esb_next_wake_ticks()==50);
 k.keys[0]=7;driver.send_keyboard(&k);nrf5_esb_task();assert(busy && sent[2]==NRF5_ESB_KEYBOARD && sent[18]==7);drain();
 // Idle background discovery backs off, while its three authentication
 // retries retain 40-unit spacing and reuse the identical HELLO packet.
 nrf5_esb_set_active(false);nrf5_esb_set_probe(true);power_idle=true;
 nrf5_esb_task();assert(busy && sent[2]==NRF5_ESB_HELLO);
 uint8_t probe[64];memcpy(probe,sent,sent_length);complete(false);
 assert(!busy);clock_ms+=40;nrf5_esb_task();assert(busy&&!memcmp(probe,sent,sent_length));complete(false);
 clock_ms+=40;nrf5_esb_task();assert(busy&&!memcmp(probe,sent,sent_length));complete(false);
 clock_ms+=1999;nrf5_esb_task();assert(!busy);
 clock_ms++;nrf5_esb_task();assert(busy && sent[2]==NRF5_ESB_HELLO);complete(true);
 power_idle=false;clock_ms+=499;nrf5_esb_task();assert(!busy);
 clock_ms++;nrf5_esb_task();assert(busy);
}
''', self.transport_headers(), ['-DNRF5_ESB_TX', '-DRAW_ENABLE', '-DNRF5_WIRELESS_POWER_ENABLE'])

    def test_receiver_usb_busy_duplicate_frames_disconnect_and_restart(self):
        self.run_c(r'''
#include <assert.h>
#include <string.h>
#include "esb_transport.c"

bool nrf5_pair_busy(void){return false;}
bool nrf5_pair_task(uint32_t now){(void)now;return false;}
void nrf5_pair_receive(const uint8_t *p,uint8_t n){(void)p;(void)n;}
void nrf5_pair_poll_key(uint32_t now){(void)now;}
void nrf5_pair_key(bool p){(void)p;}
bool nrf5_pair_cancel(void){return true;}
bool nrf5_pair_start(void){return true;}
bool nrf5_pair_confirm(void){return true;}
uint8_t nrf5_pair_state(void){return 0;}
uint8_t nrf5_pair_seconds(void){return 0;}
uint32_t nrf5_pair_fingerprint(void){return 0;}


static nrf5_esb_settings_t configuration={.paired=1,.address=17,.key={1}};
void nrf5_esb_settings_init(void){}
void nrf5_esb_settings_task(void){}
const nrf5_esb_settings_t *nrf5_esb_settings(void){return &configuration;}
bool nrf5_esb_settings_pending(void){return false;}
uint32_t nrf5_esb_settings_error(void){return 0;}
void nrf5_esb_radio_configure(bool active,uint32_t address){(void)active;(void)address;}
bool nrf5_esb_seal(const uint8_t k[16],uint64_t c,uint8_t *p,uint8_t n){(void)k;(void)c;(void)p;(void)n;return true;}
bool nrf5_esb_open(const uint8_t k[16],uint64_t c,uint8_t *p,uint8_t n){(void)k;(void)c;(void)p;(void)n;return true;}
void nrf5_esb_radio_ack(uint8_t ack[30]){memset(ack,0,30);}
bool nrf5_esb_ack_open(const uint8_t k[16],const uint8_t *p,const uint8_t a[30],uint64_t *c,uint8_t *l,bool *v){(void)k;(void)p;(void)a;*c=9;*l=2;*v=true;return true;}
bool nrf5_esb_ack_seal(const uint8_t k[16],const uint8_t *p,uint64_t c,uint8_t l,bool v,uint8_t a[30]){(void)k;(void)p;(void)c;(void)l;(void)v;memset(a,0,30);return true;}
void nrf5_esb_radio_cache_ack(const uint8_t *p,uint8_t n,const uint8_t a[30]){(void)p;(void)n;(void)a;}

static uint32_t clock_ms;
static bool blocked;
static uint8_t input[64],input_len;
static int submitted,key_downs;
static nrf5_esb_report_t last;
static uint8_t usb_leds(void){return 2;}
static host_driver_t usb={.keyboard_leds=usb_leds};
static int via_replies;
void nrf_via_send(uint8_t *p,uint8_t n){assert(n==32 && p[0]==1);via_replies++;}
host_driver_t *nrf5_usb_driver(void){return &usb;}
uint32_t timer_read32(void){return clock_ms;}
void nrf5_esb_radio_init(void){}
void nrf5_esb_radio_task(void){}
uint32_t nrf5_esb_radio_error(void){return 0;}
void nrf5_esb_radio_counters(uint32_t p[5]){memset(p,0,5*sizeof(*p));}
uint32_t sd_rand_application_vector_get(uint8_t *p,uint8_t n){memset(p,0xAB,n);return 0;}
bool nrf5_esb_radio_receive(uint8_t *p,uint8_t *n){if(!input_len)return false;memcpy(p,input,input_len);*n=input_len;input_len=0;return true;}
bool nrf5_usb_esb_report(uint8_t k,const uint8_t *p,uint8_t n){
 if(blocked)return false;last.kind=k;last.length=n;memcpy(last.data,p,n);submitted++;
 if(k==NRF5_ESB_KEYBOARD && p[2])key_downs++;return true;
}
static void packet(uint64_t boot,uint32_t seq,uint8_t key){
 static uint64_t oldboot;
 if(oldboot!=boot){nrf5_esb_report_t h={.kind=NRF5_ESB_HELLO};input_len=nrf5_esb_encode(input,boot,seq,&h);nrf5_esb_task();oldboot=boot;}
 nrf5_esb_report_t r={.kind=NRF5_ESB_KEYBOARD,.length=8,.data={0,0,key}};
 input_len=nrf5_esb_encode(input,boot,seq,&r);nrf5_esb_task();
}
static void drain(void){for(int i=0;i<40&&count;i++)nrf5_esb_task();assert(!count);}
int main(void){
 nrf5_esb_init();blocked=true;packet(1,UINT32_MAX,4);assert(nrf5_esb_connected());
 for(int i=0;i<100;i++)nrf5_esb_task();assert(submitted==0);
 blocked=false;drain();assert(key_downs==1 && last.data[2]==4);
 packet(1,UINT32_MAX,4);drain();assert(key_downs==1); // lost ACK
 packet(1,0,0);drain();assert(last.data[2]==0); // sequence wrap + release
 packet(1,UINT32_MAX-1,4);drain();assert(key_downs==1); // old packet
 clock_ms=NRF5_ESB_TIMEOUT_MS;nrf5_esb_task();assert(!connected);drain();
 assert(last.kind==NRF5_ESB_MOUSE && last.data[0]==0);
 packet(2,0,5);drain();assert(key_downs==2 && last.data[2]==5);
 // Suspension/backpressure: a bounded queue recovers with releases.
 blocked=true;for(int i=1;i<90;i++)packet(2,i,i%2?4:0);
 assert(count<=NRF5_ESB_QUEUE_SIZE);blocked=false;drain();assert(last.data[2]==4);
 clock_ms+=NRF5_ESB_TIMEOUT_MS;nrf5_esb_task();drain();assert(!connected);
 assert(driver.keyboard_leds()==2);
 uint8_t via[32]={1};driver.send_raw_hid(via,32);assert(via_replies==1);
 via[0]=0x83;nrf5_esb_diagnostics(via);assert(via[0]==0x83&&via[1]==2&&via[2]==2&&!via[3]);
 report_keyboard_t local={.keys={8}};driver.send_keyboard(&local);assert(!count);
}
''', self.transport_headers(), ['-DNRF5_ESB_RX', '-DRAW_ENABLE', '-Wno-misleading-indentation'])
