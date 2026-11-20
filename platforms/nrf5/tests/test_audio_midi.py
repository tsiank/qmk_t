# Copyright 2026 QMK Contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native sanitizer tests for the real nRF MIDI codec/queues and piezo driver."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]

class AudioMidiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.write('qmk_midi.h', '''#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint8_t Event, Data1, Data2, Data3; } MIDI_EventPacket_t;
#define MIDI_INPUT_QUEUE_LENGTH 192
#define IDLE 0
typedef struct { int input_state, input_count, input_queue; uint8_t input_queue_data[192]; } MidiDevice;
extern MidiDevice midi_device;
void bytequeue_init(int*,uint8_t*,int);
void send_midi_packet(MIDI_EventPacket_t*);
bool recv_midi_packet(MIDI_EventPacket_t*);
''')
        self.write('app_util_platform.h', '''#pragma once
#include <assert.h>
extern int critical_depth;
#define CRITICAL_REGION_ENTER() do { assert(critical_depth++ == 0); } while(0)
#define CRITICAL_REGION_EXIT() do { assert(--critical_depth == 0); } while(0)
''')
        self.write('host.h', '''#pragma once
typedef struct { int id; } host_driver_t;
host_driver_t *host_get_driver(void);
''')
        self.write('nrf5_usb.h', '#include "host.h"\nhost_driver_t *nrf5_usb_driver(void);\n')
        self.write('ble_service.h', '#include "host.h"\nhost_driver_t *nrf5_ble_driver(void);\n')
        self.write('audio.h', '''#pragma once
#include <stdint.h>
#include <stdbool.h>
void play_note(float,float); void stop_note(float); void stop_all_notes(void);
bool audio_update_state(void); float audio_get_processed_frequency(uint8_t);
''')

    def write(self, name, text):
        (self.path / name).write_text(text)

    def run_c(self, body, sources):
        self.write('test.c', body)
        cc = shutil.which('cc')
        self.assertIsNotNone(cc)
        command = [cc, '-std=gnu11', '-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                   '-DAUDIO_ENABLE', '-I', str(self.path), '-I', str(ROOT / 'platforms/nrf5'),
                   str(self.path / 'test.c'), *[str(ROOT / s) for s in sources], '-lm', '-o', str(self.path / 'test')]
        for cmd in (command, [str(self.path / 'test')]):
            result = subprocess.run(cmd, capture_output=True, text=True,
                                    env={**os.environ, 'ASAN_OPTIONS': 'detect_stack_use_after_return=1'})
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ble_codec_running_status_realtime_sysex_and_malformed(self):
        self.run_c(r'''
#include <assert.h>
#include <string.h>
#include "midi_transport.h"
static uint8_t events[32][4]; static unsigned count, resets;
void nrf_midi_receive(const uint8_t *p) { assert(count<32); memcpy(events[count++],p,4); }
void nrf_midi_reset(void) { resets++; }
int main(void) {
    // One timestamp, running status; then new timestamp + real-time inside a note.
    uint8_t input[] = {0x80,0x80,0x90,60,100,64,90,0x81,0x90,67,0x81,0xf8,80,0x82,0x80,60,0};
    nrf_midi_ble_receive(input,sizeof(input));
    assert(count==5 && !resets);
    assert(events[0][1]==0x90 && events[0][2]==60);
    assert(events[1][2]==64 && events[1][3]==90);
    assert(events[2][0]==15 && events[2][1]==0xf8);
    assert(events[3][2]==67 && events[3][3]==80);
    assert(events[4][1]==0x80);
    // Timestamp plus running status data; 2-byte program/channel pressure.
    count=0;
    uint8_t two[]={0x80,0x80,0xc2,5,0x81,6,0x82,0xd2,30};
    nrf_midi_ble_receive(two,sizeof(two));
    assert(count==3 && events[1][1]==0xc2 && events[1][2]==6 && events[2][0]==13);
    // SysEx carries unfinished bytes across packets, with timestamped EOX.
    count=0;
    uint8_t first[]={0x80,0x80,0xf0,1,2,3};
    uint8_t second[]={0x80,4,0x81,0xf8,5,0x82,0xf7};
    nrf_midi_ble_receive(first,sizeof(first));
    nrf_midi_ble_receive(second,sizeof(second));
    assert(count==4 && events[0][0]==4 && events[0][1]==0xf0);
    assert(events[1][1]==0xf8 && events[2][1]==3 && events[2][3]==5);
    assert(events[3][0]==5 && events[3][1]==0xf7);
    uint8_t encoded[6], packet[]={9,0x90,69,127};
    assert(nrf_midi_ble_encode(packet,8191,encoded)==5);
    assert(encoded[0]==0xbf && encoded[1]==0xff && encoded[2]==0x90);
    uint8_t sx[]={7,0xf0,1,0xf7};
    assert(nrf_midi_ble_encode(sx,0,encoded)==6);
    assert(encoded[4]==0x80 && encoded[5]==0xf7);
    // Decoder accepts our wire encoding, including wrap.
    count=0; nrf_midi_ble_receive(encoded,6);
    assert(count==1 && events[0][0]==7 && events[0][3]==0xf7);
    uint8_t bad[]={0x80,0x80,0x90,60};
    nrf_midi_ble_receive(bad,sizeof(bad)); assert(resets==1);
    uint8_t missing_running[]={0x80,60,100};
    nrf_midi_ble_receive(missing_running,sizeof(missing_running)); assert(resets==2);
    // Fuzz all short encodings for memory safety and recovery.
    uint8_t fuzz[8]={0x80};
    for(unsigned i=0;i<65536;i++) {
        nrf_midi_codec_reset(); count=0;
        fuzz[1]=i; fuzz[2]=i>>8; fuzz[3]=0xf7;
        nrf_midi_ble_receive(fuzz,4);
    }
}
''', ['platforms/nrf5/midi_codec.c'])

    def test_audio_notes_sustain_channels_disconnect_and_queue_backpressure(self):
        self.run_c(r'''
#include <assert.h>
#include <string.h>
#include <math.h>
#include "midi_transport.h"
#include "host.h"
MidiDevice midi_device;
int critical_depth;
static host_driver_t usb={1},ble={2},*driver=&usb;
static unsigned starts,stops,panics,sends; static float pitch; static bool blocked;
host_driver_t *host_get_driver(void){return driver;}
host_driver_t *nrf5_usb_driver(void){return &usb;}
host_driver_t *nrf5_ble_driver(void){return &ble;}
void bytequeue_init(int *a,uint8_t*b,int c){(void)a;(void)b;assert(c==192);}
void play_note(float f,float v){pitch=f;starts++;}
void stop_note(float f){pitch=f;stops++;}
void stop_all_notes(void){panics++;}
void nrf_midi_codec_reset(void){}
void nrf_midi_usb_task(void){}
void nrf_midi_ble_tick(void){}
bool nrf_midi_usb_send(const uint8_t*p){if(blocked)return false; sends++; return true;}
bool nrf_midi_ble_send(const uint8_t*p){if(blocked)return false; sends++; return true;}
int main(void){
    nrf_midi_task();
    nrf_midi_audio(0x90,69,100); assert(starts==1 && fabsf(pitch-440)<.001f);
    nrf_midi_audio(0x91,69,100); assert(starts==2);
    nrf_midi_audio(0x90,69,0); assert(stops==0); // channel 1 still holds A4
    nrf_midi_audio(0x81,69,0); assert(stops==1);
    nrf_midi_audio(0x90,60,90); assert(fabsf(pitch-261.6256f)<.01f);
    nrf_midi_audio(0xb0,64,127); nrf_midi_audio(0x80,60,0); assert(stops==1);
    nrf_midi_audio(0xb0,64,0); assert(stops==2);
    nrf_midi_audio(0x90,72,90); nrf_midi_audio(0xb0,120,0); assert(stops==3);
    unsigned before=panics;
    nrf_midi_reset(); assert(panics==before); // ISR only queues reset
    nrf_midi_task(); assert(panics==before+1);
    uint8_t packet[]={9,0x90,60,127}; MIDI_EventPacket_t event;
    // Bounded receiver prevents overflow of QMK's 192-byte input queue.
    for(unsigned i=0;i<70;i++) nrf_midi_receive(packet);
    unsigned n=0; while(recv_midi_packet(&event))n++; assert(n==32);
    nrf_midi_task(); n=0;while(recv_midi_packet(&event))n++; assert(n==32);
    nrf_midi_task(); n=0;while(recv_midi_packet(&event))n++; assert(n==6);
    // Overflow discards backlog and releases sound instead of losing only Note Off.
    for(unsigned i=0;i<150;i++)nrf_midi_receive(packet);
    assert(!recv_midi_packet(&event)); before=panics; nrf_midi_task(); assert(panics==before+1);
    blocked=true; memcpy(&event,packet,4); send_midi_packet(&event);nrf_midi_task();assert(!sends);
    blocked=false;nrf_midi_task();assert(sends==1);
    blocked=true;send_midi_packet(&event);driver=&ble;nrf_midi_task();
    blocked=false;nrf_midi_task();assert(sends==1); // no stale notes on new transport
    packet[2]=0xff;nrf_midi_receive(packet);assert(!recv_midi_packet(&event));
    assert(critical_depth==0);
}
''', ['platforms/nrf5/midi_transport.c'])

    def test_pwm_independent_instance_frequency_and_silence(self):
        self.write('timer.h', '#include <stdint.h>\nuint16_t timer_read(void); uint16_t timer_elapsed(uint16_t);\n')
        self.write('nrf_gpio.h', '#include <stdint.h>\nvoid nrf_gpio_cfg_output(uint32_t); void nrf_gpio_pin_clear(uint32_t);\n')
        self.write('nrf_delay.h', '#include <stdint.h>\nvoid nrf_delay_ms(uint32_t); unsigned __get_IPSR(void);\n')
        self.write('nrfx_pwm.h', r'''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define AUDIO_PIN 31
#define AUDIO_PIN_ALT 30
#define NRFX_PWM_INSTANCE(n) {n}
#define NRFX_PWM_DEFAULT_CONFIG {0}
#define NRF_PWM_VALUES_LENGTH(x) 4
#define NRFX_PWM_PIN_NOT_USED 255
#define NRF_PWM_CLK_1MHz 4
#define NRF_PWM_MODE_UP 0
#define NRF_PWM_LOAD_INDIVIDUAL 2
#define NRF_PWM_STEP_AUTO 0
#define NRFX_SUCCESS 0
#define NRFX_PWM_FLAG_LOOP 1
typedef struct { int id; } nrfx_pwm_t;
typedef struct { uint16_t channel_0,channel_1,channel_2,channel_3; } nrf_pwm_values_individual_t;
typedef struct { uint32_t output_pins[4]; int base_clock,count_mode,top_value,load_mode,step_mode; } nrfx_pwm_config_t;
typedef struct { union { nrf_pwm_values_individual_t *p_individual; } values; int length; } nrf_pwm_sequence_t;
int nrfx_pwm_init(const nrfx_pwm_t*,const nrfx_pwm_config_t*,void*);
bool nrfx_pwm_stop(const nrfx_pwm_t*,bool);
void nrfx_pwm_uninit(const nrfx_pwm_t*);
int nrfx_pwm_simple_playback(const nrfx_pwm_t*,const nrf_pwm_sequence_t*,int,int);
''')
        self.run_c(r'''
#include <assert.h>
#include <math.h>
#include "nrfx_pwm.h"
#include "audio.h"
void audio_driver_initialize_impl(void);void audio_driver_start_impl(void);void audio_driver_stop_impl(void);void nrf_audio_task(void);void nrf_audio_wait_ms(uint32_t);
static unsigned irq;
unsigned __get_IPSR(void){return irq;}
void nrf_delay_ms(uint32_t);
static uint16_t now,period;static float pitch;static unsigned starts,clears;static bool live;
void nrf_delay_ms(uint32_t ms){now+=ms;}
uint16_t timer_read(void){return now;}uint16_t timer_elapsed(uint16_t t){return now-t;}
bool audio_update_state(void){return true;}float audio_get_processed_frequency(uint8_t i){assert(i==0);return pitch;}
void nrf_gpio_cfg_output(uint32_t p){assert(p==30||p==31);}
void nrf_gpio_pin_clear(uint32_t p){assert(p==30||p==31);clears++;}
int nrfx_pwm_init(const nrfx_pwm_t*p,const nrfx_pwm_config_t*c,void*h){
 assert(p->id==1 && !live);assert(c->output_pins[0]==31 && c->output_pins[1]==30);
 assert(c->output_pins[2]==255 && c->output_pins[3]==255);
 assert(c->base_clock==NRF_PWM_CLK_1MHz && c->load_mode==NRF_PWM_LOAD_INDIVIDUAL);
 period=c->top_value;live=true;return 0;}
bool nrfx_pwm_stop(const nrfx_pwm_t*p,bool wait){assert(p->id==1&&wait);return true;}
void nrfx_pwm_uninit(const nrfx_pwm_t*p){live=false;}
int nrfx_pwm_simple_playback(const nrfx_pwm_t*p,const nrf_pwm_sequence_t*s,int n,int flags){
 assert(p->id==1 && n==1 && flags==NRFX_PWM_FLAG_LOOP && s->length==4);
 assert(s->values.p_individual->channel_0==period/2);
 assert(s->values.p_individual->channel_1==(0x8000|period/2));starts++;return 0;}
int main(void){
 audio_driver_initialize_impl();assert(clears==2);
 pitch=440;audio_driver_start_impl();nrf_audio_task();assert(live && period==2273 && starts==1);
 now+=4;nrf_audio_task();assert(starts==1);
 pitch=880;now+=4;nrf_audio_task();assert(period==1136 && starts==2);
 pitch=0;now+=4;nrf_audio_task();assert(!live);
 pitch=NAN;now+=4;nrf_audio_task();assert(!live);
 pitch=440;now=65534;audio_driver_start_impl();nrf_audio_task();assert(live);
 now=2;pitch=880;nrf_audio_task();assert(period==1136);
 audio_driver_stop_impl();assert(!live);now+=4;nrf_audio_task();assert(!live);
 pitch=440;audio_driver_start_impl();nrf_audio_wait_ms(4);assert(live&&period==2273);
 pitch=880;irq=1;nrf_audio_wait_ms(4);assert(period==2273);
 irq=0;nrf_audio_task();assert(period==1136);
 audio_driver_stop_impl();
}
''', ['platforms/nrf5/drivers/audio_pwm_hardware.c'])

    def test_usb_descriptors_and_endpoint_lifecycle(self):
        self.write('app_usbd_core.h', '')
        self.write('app_error.h', '#define APP_ERROR_CHECK(x) assert((x)==0)\n')
        self.write('app_usbd.h', r"""#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
typedef unsigned ret_code_t;
typedef unsigned nrf_drv_usbd_ep_status_t;
#define NRF_SUCCESS 0
#define NRF_ERROR_NOT_SUPPORTED 1
#define NRF_ERROR_INVALID_PARAM 2
#define NRF_ERROR_INVALID_LENGTH 3
#define NRF_DRV_USBD_EPIN5 0x85
#define NRF_DRV_USBD_EPOUT5 5
#define NRF_USBD_EP_OK 0
#define NRF_USBD_EP_ABORTED 1
#define NRF_USBD_EP_WAITING 2
enum {APP_USBD_EVT_DRV_SETUP,APP_USBD_EVT_DRV_SUSPEND,APP_USBD_EVT_DRV_RESUME,
 APP_USBD_EVT_DRV_RESET,APP_USBD_EVT_STOPPED,APP_USBD_EVT_POWER_REMOVED,APP_USBD_EVT_DRV_EPTRANSFER};
typedef struct { int unused; } app_usbd_class_inst_t;
typedef struct { struct { int type; } app_evt; struct { struct { struct { uint8_t ep; unsigned status; } eptransfer; } data; } drv_evt; } app_usbd_complex_evt_t;
typedef struct { unsigned line; uint8_t data_buffer; } app_usbd_class_descriptor_ctx_t;
typedef struct { uint8_t *p_buffer; size_t current_size, maximum_size; app_usbd_class_descriptor_ctx_t *p_context; } app_usbd_class_descriptor_state_t;
typedef struct { const void *data; size_t size; } transfer_t;
#define NRF_DRV_USBD_TRANSFER_OUT(name,p,n) transfer_t name={p,n}
#define NRF_DRV_USBD_TRANSFER_IN(name,p,n) transfer_t name={p,n}
#define APP_USBD_CLASS_TYPEDEF(name,...) typedef struct { app_usbd_class_inst_t base; } name##_t
#define APP_USBD_CLASS_INST_DEF(name,type,...) static type##_t name
struct methods { ret_code_t(*event_handler)(const app_usbd_class_inst_t*,const app_usbd_complex_evt_t*);
 bool(*feed_descriptors)(app_usbd_class_descriptor_ctx_t*,const app_usbd_class_inst_t*,uint8_t*,size_t);
 ret_code_t(*iface_select)(const app_usbd_class_inst_t*,uint8_t,uint8_t);
 void(*iface_deselect)(const app_usbd_class_inst_t*,uint8_t); };
typedef struct methods app_usbd_class_methods_t;
void nrf_drv_usbd_ep_abort(uint8_t);
void app_usbd_ep_enable(uint8_t); size_t nrf_drv_usbd_epout_size_get(uint8_t);
unsigned app_usbd_ep_transfer(uint8_t,const transfer_t*);
unsigned app_usbd_class_append(const app_usbd_class_inst_t*);
""")
        # Exercise the SDK's actual resumable descriptor feeder macros, including
        # splits across the 64-byte EP0 boundary, rather than a mock byte copier.
        header = (ROOT/'lib/nrf5/components/libraries/usbd/app_usbd_class_base.h').read_text()
        macros = header[header.index('#define APP_USBD_CLASS_DESCRIPTOR_BEGIN'):header.index('/** @} */',header.index('#define APP_USBD_CLASS_DESCRIPTOR_BEGIN'))]
        with (self.path/'app_usbd.h').open('a') as f:
            f.write('\n#define ASSERT assert\n' + macros)
        self.run_c(r"""
#include <assert.h>
#include <string.h>
#include "app_usbd.h"
#include "midi_transport.h"
static unsigned resets,received;static uint8_t received_data[4];
static size_t wire_size; static uint8_t wire[64], sent[4]; static bool fail;
void nrf_midi_reset(void){resets++;}
void nrf_midi_receive(const uint8_t *p){received++;memcpy(received_data,p,4);}
void nrf_drv_usbd_ep_abort(uint8_t ep){assert(ep==0x85);}
void app_usbd_ep_enable(uint8_t ep){assert(ep==5||ep==0x85);}
size_t nrf_drv_usbd_epout_size_get(uint8_t ep){assert(ep==5);return wire_size;}
unsigned app_usbd_ep_transfer(uint8_t ep,const transfer_t*t){
 if(fail)return 2;
 if(ep==5){assert(t->size==wire_size);memcpy((void*)t->data,wire,wire_size);}
 else {assert(ep==0x85 && t->size==4);memcpy(sent,t->data,4);}return 0;
}
unsigned app_usbd_class_append(const app_usbd_class_inst_t*p){return 0;}
#include """ + '"' + str(ROOT/'tmk_core/protocol/nrf5/usb/midi_usb.c') + '"' + r"""
static void event(int type,uint8_t ep,unsigned status){
 app_usbd_complex_evt_t e={0};e.app_evt.type=type;e.drv_evt.data.eptransfer.ep=ep;e.drv_evt.data.eptransfer.status=status;
 assert(event_handler(&midi_instance.base,&e)==0);
}
int main(void){
 assert(sizeof(descriptors)==92);
 // Walk all descriptors by bLength: consecutive AudioControl/MIDIStreaming,
 // one 64-byte bulk endpoint each way, and correct class-specific total.
 unsigned interfaces=0,eps=0;size_t ms=0;
 for(size_t i=0;i<sizeof(descriptors);i+=descriptors[i]){
  assert(descriptors[i]>=5 && i+descriptors[i]<=sizeof(descriptors));
  if(descriptors[i+1]==4){interfaces++;assert(descriptors[i+5]==1);
   if(interfaces==1)assert(descriptors[i+2]==4&&descriptors[i+6]==1);
   else {assert(descriptors[i+2]==5&&descriptors[i+6]==3);ms=i+9;}}
  if(descriptors[i+1]==5){eps++;assert(descriptors[i+3]==2&&descriptors[i+4]==64);}
 }
 assert(interfaces==2&&eps==2&&sizeof(descriptors)-ms==descriptors[ms+5]);
 app_usbd_class_descriptor_ctx_t context={0}; uint8_t buffer[64]={0};
 assert(feed(&context,&midi_instance.base,buffer,64));assert(!memcmp(buffer,descriptors,64));
 assert(feed(&context,&midi_instance.base,buffer,64));assert(!memcmp(buffer,descriptors+64,sizeof(descriptors)-64));
 assert(!feed(&context,&midi_instance.base,buffer,64));
 nrf_midi_usb_init(); assert(select_interface(&midi_instance.base,1,1)!=0);
 assert(select_interface(&midi_instance.base,1,0)==0);
 uint8_t note[]={9,0x90,69,100};assert(nrf_midi_usb_send(note));assert(!memcmp(sent,note,4));
 note[2]=70;assert(!nrf_midi_usb_send(note));assert(sent[2]==69); // DMA ownership
 event(APP_USBD_EVT_DRV_EPTRANSFER,0x85,NRF_USBD_EP_OK);
 assert(nrf_midi_usb_send(note));assert(sent[2]==70);
 memcpy(wire,note,4);wire_size=4;event(APP_USBD_EVT_DRV_EPTRANSFER,5,NRF_USBD_EP_WAITING);
 event(APP_USBD_EVT_DRV_EPTRANSFER,5,NRF_USBD_EP_OK);assert(received==1&&!memcmp(note,received_data,4));
 wire_size=3;event(APP_USBD_EVT_DRV_EPTRANSFER,5,NRF_USBD_EP_WAITING);
 event(APP_USBD_EVT_DRV_EPTRANSFER,5,NRF_USBD_EP_OK);assert(resets==1&&received==1);
 event(APP_USBD_EVT_DRV_SUSPEND,0,0);assert(suspended&&resets==2);
 event(APP_USBD_EVT_DRV_RESUME,0,0);
 for(unsigned ch=0;ch<16;ch++){
  nrf_midi_usb_task();assert(sent[0]==11&&sent[1]==(0xb0|ch)&&sent[2]==120);
  event(APP_USBD_EVT_DRV_EPTRANSFER,0x85,NRF_USBD_EP_OK);
 }
 assert(!panic_remaining);
 event(APP_USBD_EVT_DRV_RESET,0,0);assert(!configured&&!tx_busy);
 deselect_interface(&midi_instance.base,1);assert(!configured);
}
""", [])

    def test_ble_registration_handshake_bond_restore_and_backpressure(self):
        self.write('app_timer.h', '#include <stdint.h>\n#define APP_TIMER_CLOCK_FREQ 32768\nuint32_t app_timer_cnt_get(void); uint32_t app_timer_cnt_diff_compute(uint32_t,uint32_t);\n')
        self.write('host_driver.h', '#include "host.h"\n')
        self.write('nrf_drv_saadc.h', '#include <stdint.h>\ntypedef int16_t nrf_saadc_value_t;\n')
        self.write('timer.h', '#include <stdint.h>\nuint16_t timer_read(void);\n')
        self.write('nrf_sdh_ble.h', '#include <stddef.h>\n#define NRF_SDH_BLE_OBSERVER(...)\n#define NRF_SDH_BLE_GATT_MAX_MTU_SIZE 247\n')
        self.write('app_error.h', '#include <assert.h>\n#define APP_ERROR_CHECK(x) assert((x)==0)\n')
        self.write('ble.h', r"""#pragma once
#include <stdint.h>
#include <stdbool.h>
#define BLE_CONN_HANDLE_INVALID 65535
#define BLE_GATTS_EVT_WRITE 1
#define BLE_GAP_EVT_DISCONNECTED 2
#define BLE_GAP_EVT_CONNECTED 3
#define BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST 4
#define BLE_GATTS_AUTHORIZE_TYPE_READ 1
#define BLE_GATT_STATUS_SUCCESS 0
#define BLE_GATTS_OP_WRITE_REQ 1
#define BLE_GATTS_OP_WRITE_CMD 2
#define BLE_GATTS_SRVC_TYPE_PRIMARY 1
#define BLE_GATT_HVX_NOTIFICATION 1
#define NRF_SUCCESS 0
#define NRF_ERROR_RESOURCES 19
#define NRF_ERROR_BUSY 17
typedef uint32_t ret_code_t;
typedef struct { uint8_t uuid128[16]; } ble_uuid128_t;
typedef struct { uint16_t uuid; uint8_t type; } ble_uuid_t;
typedef struct { uint16_t value_handle,cccd_handle; } ble_gatts_char_handles_t;
typedef struct { uint16_t handle,offset,len; uint8_t op,data[244]; } ble_gatts_evt_write_t;
typedef struct { unsigned type; struct { struct { uint16_t handle; } read; } request; } ble_gatts_evt_rw_authorize_request_t;
typedef struct { unsigned type; struct { struct { unsigned gatt_status,update,len; } read; } params; } ble_gatts_rw_authorize_reply_params_t;
typedef struct { uint16_t len,offset; uint8_t *p_value; } ble_gatts_value_t;
typedef struct { uint16_t handle,type,*p_len; uint8_t *p_data; } ble_gatts_hvx_params_t;
typedef struct { struct { unsigned evt_id; } header; struct { struct { uint16_t conn_handle; } gap_evt;
 struct { struct { ble_gatts_evt_write_t write; ble_gatts_evt_rw_authorize_request_t authorize_request; } params; } gatts_evt; } evt; } ble_evt_t;
uint32_t sd_ble_uuid_vs_add(const ble_uuid128_t*,uint8_t*);
uint32_t sd_ble_gatts_service_add(uint8_t,const ble_uuid_t*,uint16_t*);
uint32_t sd_ble_gatts_value_get(uint16_t,uint16_t,ble_gatts_value_t*);
uint32_t sd_ble_gatts_hvx(uint16_t,const ble_gatts_hvx_params_t*);
uint32_t sd_ble_gatts_rw_authorize_reply(uint16_t,const ble_gatts_rw_authorize_reply_params_t*);
""")
        self.write('ble_srv_common.h', r"""#include "ble.h"
#define SEC_JUST_WORKS 2
typedef struct { uint16_t uuid,max_len,init_len; uint8_t uuid_type; bool is_var_len,is_defered_read;
 struct { unsigned read,write,write_wo_resp,notify; } char_props; unsigned read_access,write_access,cccd_write_access; } ble_add_char_params_t;
uint32_t characteristic_add(uint16_t,const ble_add_char_params_t*,ble_gatts_char_handles_t*);
static inline bool ble_srv_is_notification_enabled(const uint8_t*p){return p[0]&1;}
""")
        self.run_c('#include "' + str(ROOT/'tmk_core/protocol/nrf5/ble/midi_gatt.c') + '"\n' + r"""
#include <string.h>
static unsigned bases,authorized,resets,decoded,activity,notifications,notify_error;
static bool restored;
static uint32_t ticks;
uint32_t app_timer_cnt_get(void){return ticks;}
uint32_t app_timer_cnt_diff_compute(uint32_t now,uint32_t before){return (now-before)&0xffffff;}
void reset_power_save_counter(void){activity++;}
void nrf_midi_reset(void){resets++;}void nrf_midi_codec_reset(void){}
void nrf_midi_ble_receive(const uint8_t*p,size_t n){decoded++;assert(n==5);}
size_t nrf_midi_ble_encode(const uint8_t*p,uint16_t t,uint8_t*out){assert(t==8191);memset(out,0,5);return 5;}
uint32_t sd_ble_uuid_vs_add(const ble_uuid128_t*p,uint8_t*t){
 if(!bases)assert(p->uuid128[15]==3 && p->uuid128[0]==0);
 else assert(p->uuid128[15]==0x77&&p->uuid128[0]==0xf3);
 *t=2+bases++;return 0;}
uint32_t sd_ble_gatts_service_add(uint8_t kind,const ble_uuid_t*u,uint16_t*h){assert(u->uuid==0x0e5a&&u->type==2);*h=10;return 0;}
uint32_t characteristic_add(uint16_t h,const ble_add_char_params_t*p,ble_gatts_char_handles_t*out){
 assert(h==10 && p->uuid==0xe5db && p->uuid_type==3 && p->max_len==244);
 assert(p->is_var_len && p->is_defered_read && p->init_len==0);
 assert(p->char_props.read && p->char_props.write_wo_resp && p->char_props.notify);
 assert(p->read_access==2 && p->write_access==2 && p->cccd_write_access==2);
 out->value_handle=11;out->cccd_handle=12;return 0;}
uint32_t sd_ble_gatts_value_get(uint16_t c,uint16_t h,ble_gatts_value_t*v){
 assert(c==7 && h==12 && v->len==2);v->p_value[0]=restored;v->p_value[1]=0;return 0;}
uint32_t sd_ble_gatts_hvx(uint16_t c,const ble_gatts_hvx_params_t*p){
 assert(c==7&&p->handle==11&&*p->p_len==5);notifications++;return notify_error;}
uint32_t sd_ble_gatts_rw_authorize_reply(uint16_t c,const ble_gatts_rw_authorize_reply_params_t*p){
 assert(c==7&&p->type==BLE_GATTS_AUTHORIZE_TYPE_READ&&p->params.read.update&&p->params.read.len==0);authorized++;return 0;}
int main(void){
 ticks=32768;nrf_midi_ble_tick();assert(midi_timestamp()==1000);
 ticks=(8191u*32768+999)/1000;nrf_midi_ble_tick();assert(midi_timestamp()==8191);
 nrf_midi_gatt_init();assert(bases==2 && nrf_midi_service_uuid.uuid==0x0e5a);
 uint8_t note[4]={9,0x90,69,127};assert(nrf_midi_ble_send(note)&&!notifications);
 ble_evt_t e={.header.evt_id=BLE_GAP_EVT_CONNECTED};e.evt.gap_evt.conn_handle=7;midi_event(&e,0);
 assert(nrf_midi_ble_send(note)&&!notifications);
 restored=true;assert(nrf_midi_ble_send(note)&&notifications==1); // no CCCD write after bonding
 notify_error=NRF_ERROR_RESOURCES;assert(!nrf_midi_ble_send(note));
 notify_error=NRF_ERROR_BUSY;assert(!nrf_midi_ble_send(note));
 e.header.evt_id=BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST;e.evt.gatts_evt.params.authorize_request.type=1;
 e.evt.gatts_evt.params.authorize_request.request.read.handle=11;midi_event(&e,0);assert(authorized==1);
 e.header.evt_id=BLE_GATTS_EVT_WRITE;e.evt.gatts_evt.params.write.handle=11;
 e.evt.gatts_evt.params.write.op=BLE_GATTS_OP_WRITE_CMD;e.evt.gatts_evt.params.write.len=5;
 midi_event(&e,0);assert(decoded==1&&activity==1);
 e.evt.gatts_evt.params.write.offset=1;midi_event(&e,0);assert(decoded==1);
 e.header.evt_id=BLE_GAP_EVT_DISCONNECTED;midi_event(&e,0);assert(resets==1);
 assert(nrf_midi_ble_send(note));
 ticks=0xfffff0;nrf_midi_ble_tick();uint32_t before=elapsed_ticks;
 ticks=0x10;nrf_midi_ble_tick();assert(elapsed_ticks-before==32);
}
""", [])

    def test_qmk_usb_encoding_system_common_and_realtime(self):
        source=(ROOT/'quantum/midi/qmk_midi.c').read_text()
        start=source.index('#define SYSEX_START_OR_CONT')
        stop=source.index('static void usb_get_midi')
        self.run_c(r"""
#include <assert.h>
#include "qmk_midi.h"
#define MIDI_EVENT(c,s) (((c)<<4)|((s)>>4))
#define UNDEFINED 0
#define SYSEX_END 0xf7
#define MIDI_SONGPOSITION 0xf2
#define MIDI_SONGSELECT 0xf3
#define MIDI_TC_QUARTERFRAME 0xf1
#define MIDI_TUNEREQUEST 0xf6
static MIDI_EventPacket_t sent;
void send_midi_packet(MIDI_EventPacket_t*p){sent=*p;}
static int midi_packet_length(uint8_t s){return s==0xf0||s==0xf7||s<0x80?0:1;}
""" + source[start:stop] + r"""
int main(void){
 usb_send_func(0,3,0xf2,1,2);assert(sent.Event==3&&sent.Data3==2);
 usb_send_func(0,2,0xf1,1,0);assert(sent.Event==2);
 usb_send_func(0,2,0xf3,1,0);assert(sent.Event==2);
 usb_send_func(0,1,0xf6,0,0);assert(sent.Event==5);
 usb_send_func(0,1,0xf8,0,0);assert(sent.Event==15);
 usb_send_func(0,3,0x92,60,127);assert(sent.Event==9&&sent.Data1==0x92);
 usb_send_func(0,2,0xc2,5,0);assert(sent.Event==12);
 usb_send_func(0,3,0xf0,1,2);assert(sent.Event==4);
 usb_send_func(0,2,1,0xf7,0);assert(sent.Event==6);
}
""", [])

if __name__ == '__main__':
    unittest.main()
