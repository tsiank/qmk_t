# SPDX-License-Identifier: GPL-2.0-or-later
"""Real two-node C pairer + encrypted transport with independent crypto backend.

Only Nordic/Oberon primitives and radio/Flash peripherals are simulated. These
checks exercise lost replies, the receiver confirmation gate and saving order.
"""
import ctypes as C
import hashlib
import hmac
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_esb import HOST

ROOT = Path(__file__).resolve().parents[3]
PLATFORM = ROOT/'platforms/nrf5'
OBERON = ROOT/'lib/nrf5/external/nrf_oberon'
B = C.POINTER(C.c_uint8)

class PairingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'): raise unittest.SkipTest('Native C compiler required')
        try:
            from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
            from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
            from cryptography.hazmat.primitives.kdf.hkdf import HKDF
            from cryptography.hazmat.primitives import hashes
            from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        except ImportError: raise unittest.SkipTest('Python cryptography required')
        cls.temp=tempfile.TemporaryDirectory();w=Path(cls.temp.name)
        cls.callback_type=C.CFUNCTYPE(None,C.c_int,B,C.c_size_t,B,C.c_size_t,B,C.c_size_t,B,C.c_size_t)
        def backend(op,out,size,a,na,b,nb,c,nc):
            a,b,c=bytes(a[:na]),bytes(b[:nb]),bytes(c[:nc])
            if op==0: result=X25519PrivateKey.from_private_bytes(a).public_key().public_bytes(Encoding.Raw,PublicFormat.Raw)
            elif op==1:
                try: result=X25519PrivateKey.from_private_bytes(a).exchange(X25519PublicKey.from_public_bytes(b))
                except ValueError: result=bytes(32)
            elif op==2: result=HKDF(algorithm=hashes.SHA256(),length=size,salt=b,info=c).derive(a)
            elif op==3: result=hmac.new(a,b,hashlib.sha256).digest()
            else:
                aes=Cipher(algorithms.AES(a),modes.ECB()).encryptor();result=aes.update(b)+aes.finalize()
            C.memmove(out,result,size)
        cls.backend=cls.callback_type(backend)
        (w/'host_driver.h').write_text(HOST)
        (w/'timer.h').write_text('#include <stdint.h>\nuint32_t timer_read32(void);')
        (w/'matrix.h').write_text('#include <stdbool.h>\n#include <stdint.h>\nbool matrix_is_on(uint8_t,uint8_t);')
        (w/'nrf5_usb.h').write_text('#include "host_driver.h"\nhost_driver_t *nrf5_usb_driver(void);\nbool nrf5_usb_esb_report(uint8_t,const uint8_t *,uint8_t);')
        (w/'nrf_soc.h').write_text('''#pragma once
#include <stdint.h>
#define NRF_SUCCESS 0
typedef struct {uint8_t key[16],cleartext[16],ciphertext[16];} nrf_ecb_hal_data_t;
uint32_t sd_ecb_block_encrypt(nrf_ecb_hal_data_t *);
uint32_t sd_rand_application_vector_get(uint8_t *,uint8_t);
''')
        (w/'action.h').write_text('#pragma once\n#include <stdbool.h>\ntypedef struct {struct {bool pressed;} event;} keyrecord_t;')
        (w/'host.h').write_text('#pragma once\n#include "host_driver.h"\nhost_driver_t *host_get_driver(void);\nvoid host_set_driver(host_driver_t *);')
        (w/'action_util.h').write_text('void clear_keyboard(void);')
        (w/'ble_service.h').write_text('void advertising_start(bool);\nvoid ble_disconnect(void);')
        (w/'ble_hids_service.h').write_text('#include "host_driver.h"\nhost_driver_t *nrf5_ble_driver(void);\nbool nrf5_ble_can_send_nkro(void);')
        (w/'keycodes.h').write_text('#define KC_ESCAPE 41\n#define QK_OUTPUT_USB 1\n#define QK_OUTPUT_BLUETOOTH 2\n#define QK_OUTPUT_NONE 3\n#define QK_OUTPUT_AUTO 4\n#define QK_OUTPUT_2P4GHZ 5\n#define QK_OUTPUT_NEXT 6\n#define QK_OUTPUT_PREV 7')
        (w/'nrf.h').write_text('#pragma once\n#include <stdint.h>\nstatic struct {uint32_t USBREGSTATUS;} power;\nstatic struct {uint32_t ENABLE;} usbd;\n#define NRF_POWER (&power)\n#define NRF_USBD (&usbd)\n#define POWER_USBREGSTATUS_VBUSDETECT_Msk 1')
        (w/'crypto.c').write_text('''#include <string.h>
#include "nrf_soc.h"
#include "occ_curve25519.h"
#include "occ_hmac_sha256.h"
#include "occ_hkdf_sha256.h"
static void (*backend)(int,uint8_t *,size_t,const uint8_t *,size_t,const uint8_t *,size_t,const uint8_t *,size_t);
void fixture_backend(void (*fn)(int,uint8_t *,size_t,const uint8_t *,size_t,const uint8_t *,size_t,const uint8_t *,size_t)){backend=fn;}
void occ_curve25519_scalarmult_base(uint8_t out[32],const uint8_t priv[32]){backend(0,out,32,priv,32,0,0,0,0);}
void occ_curve25519_scalarmult(uint8_t out[32],const uint8_t priv[32],const uint8_t pub[32]){backend(1,out,32,priv,32,pub,32,0,0);}
void occ_hmac_sha256(uint8_t out[32],const uint8_t *k,size_t nk,const uint8_t *p,size_t np){backend(3,out,32,k,nk,p,np,0,0);}
void occ_hkdf_sha256(uint8_t *out,size_t n,const uint8_t *k,size_t nk,const uint8_t *s,size_t ns,const uint8_t *i,size_t ni){backend(2,out,n,k,nk,s,ns,i,ni);}
uint32_t sd_ecb_block_encrypt(nrf_ecb_hal_data_t *b){backend(4,b->ciphertext,16,b->key,16,b->cleartext,16,0,0);return 0;}
''')
        (w/'fixture.c').write_text(r'''
#include <assert.h>
#include "esb_transport.c"
#include "esb_pair.c"
#ifdef FIXTURE_ROUTING
#include "outputselect.c"
#endif
static nrf5_esb_settings_t config={.paired=1,.address=17,
#ifdef FIXTURE_ROUTING
 .mode=0,
#else
 .mode=3,
#endif
 .key={1,2,3}};
static nrf5_esb_settings_t persisted;
static uint32_t clock_ms;
static uint64_t random_state;
static uint8_t input[64],input_length,wire[64],wire_length,cached_packet[64],cached_length,cached_reply[64],cached_reply_length,result_reply[64],result_length;
static bool result_ready,result_ack,radio_enabled,pair_pipe,ctrls,pending,auto_commit=true;
static uint32_t radio_address;
static uint8_t latest_key;
#ifdef FIXTURE_BRIDGE
#include "esb_bridge.h"
static unsigned bridge_events[12],bridge_resets;
static uint8_t bridge_last[32],bridge_last_length;
static uint16_t bridge_last_token;
static bool bridge_blocked;
#ifdef NRF5_ESB_TX
static uint8_t bridge_via[32];static uint16_t bridge_token;
#endif
void nrf5_bridge_link_reset(void){bridge_resets++;}
bool nrf5_bridge_down_deliver(uint8_t kind,const uint8_t *p,uint8_t n){
 if(bridge_blocked)return false;
 bridge_events[kind]++;memcpy(bridge_last,p,n);bridge_last_length=n;
 #ifdef NRF5_ESB_TX
 if(kind==NRF5_ESB_DOWN_VIA){
  uint16_t token=(uint16_t)p[0]<<8|p[1];
  if(!p[2]){bridge_token=token;memcpy(bridge_via,p+4,16);}
  else {assert(token==bridge_token);memcpy(bridge_via+16,p+4,16);assert(nrf5_bridge_send(NRF5_BRIDGE_VIA,token,bridge_via,32));}
 }
 #endif
 return true;
}
bool nrf5_bridge_up_deliver(uint8_t channel,uint16_t token,const uint8_t *p,uint8_t n){
 if(bridge_blocked)return false;
 bridge_events[channel]++;memcpy(bridge_last,p,n);bridge_last_length=n;bridge_last_token=token;return true;
}
unsigned fixture_bridge_events(uint8_t channel){return bridge_events[channel];}
unsigned fixture_bridge_resets(void){return bridge_resets;}
uint8_t fixture_bridge_last(uint8_t *p){memcpy(p,bridge_last,bridge_last_length);return bridge_last_length;}
uint16_t fixture_bridge_token(void){return bridge_last_token;}
void fixture_bridge_block(bool value){bridge_blocked=value;}
bool fixture_bridge_push(uint8_t kind,uint16_t token,const uint8_t *p,uint8_t n){
#ifdef NRF5_ESB_TX
 return nrf5_bridge_send(kind,token,p,n);
#else
 if(kind==NRF5_ESB_DOWN_VIA)return nrf5_bridge_via_request(token,p);
 return nrf5_bridge_submit(kind,p,n,kind==NRF5_ESB_DOWN_MUSIC&&p[1]==1);
#endif
}
#endif

static int writes,persist_delay;
uint32_t timer_read32(void){return clock_ms;}
uint32_t sd_rand_application_vector_get(uint8_t *p,uint8_t n){for(uint8_t i=0;i<n;i++){random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;p[i]=random_state>>8;}return 0;}
bool matrix_is_on(uint8_t r,uint8_t c){return ctrls&&r==4&&(c==0||c==10);}
void nrf5_esb_settings_init(void){persisted=config;}
void nrf5_esb_settings_task(void){if(pending&&auto_commit&&++persist_delay>=3){persisted=config;pending=false;}}
const nrf5_esb_settings_t *nrf5_esb_settings(void){return &config;}
bool nrf5_esb_settings_pending(void){return pending;}
uint32_t nrf5_esb_settings_error(void){return 0;}
bool nrf5_esb_settings_pair(const uint8_t k[16],uint32_t a){memcpy(config.key,k,16);config.address=a;config.paired=1;pending=true;persist_delay=0;writes++;return true;}
bool nrf5_esb_settings_mode(uint8_t m){config.mode=m;pending=true;persist_delay=0;return true;}
void nrf5_esb_radio_init(void){}
void nrf5_esb_radio_task(void){}
void nrf5_esb_radio_configure(bool active,uint32_t address){
 if(radio_address!=address||radio_enabled!=active)cached_length=0;
 radio_address=address;radio_enabled=active;
}
void nrf5_esb_radio_listen_pair(bool on){pair_pipe=on;}
bool nrf5_esb_radio_send(const uint8_t *p,uint8_t n){assert(!wire_length);memcpy(wire,p,n);wire_length=n;return true;}
bool nrf5_esb_radio_result(bool *ack){if(!result_ready)return false;*ack=result_ack;result_ready=false;wire_length=0;return true;}
bool nrf5_esb_radio_receive(uint8_t *p,uint8_t *n){if(!input_length)return false;memcpy(p,input,input_length);*n=input_length;input_length=0;return true;}
void nrf5_esb_radio_ack(uint8_t ack[30]){if((result_length==30||result_length==64)&&result_reply[0]==0xA2)memcpy(ack,result_reply,30);else memset(ack,0,30);}
uint8_t nrf5_esb_radio_reply(uint8_t reply[64]){memcpy(reply,result_reply,result_length);return result_length;}
void nrf5_esb_radio_cache_reply(const uint8_t *p,uint8_t n,const uint8_t *reply,uint8_t m){memcpy(cached_packet,p,n);cached_length=n;memcpy(cached_reply,reply,m);cached_reply_length=m;}
void nrf5_esb_radio_cache_ack(const uint8_t *p,uint8_t n,const uint8_t ack[30]){nrf5_esb_radio_cache_reply(p,n,ack,30);}
uint32_t nrf5_esb_radio_error(void){return 0;}
void nrf5_esb_radio_counters(uint32_t c[5]){memset(c,0,20);}
#ifdef FIXTURE_ROUTING
static host_driver_t *host;
static uint8_t ble_key,usb_key;
static unsigned advertisements;
static bool ble_link;
static void ble_keyboard(report_keyboard_t *r){ble_key=r->keys[0];}
static void usb_keyboard(report_keyboard_t *r){usb_key=r->keys[0];}
static host_driver_t ble={.send_keyboard=ble_keyboard};
host_driver_t *host_get_driver(void){return host;}
void host_set_driver(host_driver_t *p){host=p;}
host_driver_t *nrf5_ble_driver(void){return &ble;}
bool nrf5_ble_can_send_nkro(void){return true;}
void clear_keyboard(void){report_keyboard_t r={0};if(host&&host->send_keyboard)host->send_keyboard(&r);}
void ble_disconnect(void){ble_link=false;}
void advertising_start(bool erase){assert(!erase&&nrf5_output_ble_allowed());ble_link=true;advertisements++;}
void fixture_mode(uint8_t mode){assert(nrf5_output_request(mode));}
uint8_t fixture_mode_current(void){return nrf5_output_mode();}
uint8_t fixture_mode_preferred(void){return nrf5_esb_settings()->mode;}
uint8_t fixture_ble_key(void){return ble_key;}
uint8_t fixture_usb_key(void){return usb_key;}
unsigned fixture_advertisements(void){return advertisements;}
void fixture_usb(bool plugged){power.USBREGSTATUS=plugged;usbd.ENABLE=plugged;}
#endif
static uint8_t host_led_state=2;
static uint8_t usb_leds(void){return host_led_state;}
void fixture_host_leds(uint8_t state){host_led_state=state;}
uint8_t fixture_rf_leds(void){return driver.keyboard_leds();}
static host_driver_t usb={.keyboard_leds=usb_leds,
#ifdef FIXTURE_ROUTING
 .send_keyboard=usb_keyboard,
#endif
};
host_driver_t *nrf5_usb_driver(void){return &usb;}
bool nrf5_usb_esb_report(uint8_t k,const uint8_t *p,uint8_t n){(void)n;if(k==1)latest_key=p[2];return true;}
void fixture_init(uint64_t seed){random_state=seed;nrf5_esb_init();
#ifdef FIXTURE_ROUTING
 nrf5_output_init();
#endif
}
void fixture_tick(uint32_t t){clock_ms=t;nrf5_esb_task();
#ifdef FIXTURE_ROUTING
 nrf5_output_task();
#elif defined(NRF5_ESB_TX)
 nrf5_esb_set_active(!nrf5_pair_busy()&&config.mode==3);
#endif
}
uint8_t fixture_wire(uint8_t *p){memcpy(p,wire,wire_length);return wire_length;}
uint32_t fixture_address(void){return radio_address;}
bool fixture_accepts(uint32_t address){return radio_enabled&&(address==radio_address||(pair_pipe&&address==NRF5_ESB_PAIR_ADDRESS));}
void fixture_put(const uint8_t *p,uint8_t n){memcpy(input,p,n);input_length=n;}
uint8_t fixture_reply(const uint8_t *p,uint8_t n,uint8_t *out){if(n!=cached_length||memcmp(p,cached_packet,n))return 0;memcpy(out,cached_reply,cached_reply_length);return cached_reply_length;}
void fixture_result(const uint8_t *p,uint8_t n){if(n)memcpy(result_reply,p,n);result_length=n;result_ack=n!=0;result_ready=true;}
void fixture_key(uint8_t code){report_keyboard_t r={0};r.keys[0]=code;
#ifdef FIXTURE_ROUTING
 host_get_driver()->send_keyboard(&r);
#else
 driver.send_keyboard(&r);
#endif
}
uint8_t fixture_latest(void){return latest_key;}
uint32_t fixture_saved_address(void){return persisted.address;}
int fixture_writes(void){return writes;}
void fixture_receiver_reboot(void){
#ifdef NRF5_ESB_RX
 challenge=0;connected=have_peer=false;peer_session=0;peer_sequence=0;
 head=count=0;cached_length=input_length=0;radio_enabled=false;
#endif
}
void fixture_hold_ctrl(bool on){ctrls=on;}
void fixture_unbind(void){config.paired=0;persisted=config;}
void fixture_auto_commit(bool on){auto_commit=on;}
void fixture_key_material(uint8_t *k){memcpy(k,persisted.key,16);}
''')
        cls.work=w
        for role in ('TX','RX','DONGLE','AUTO','BRIDGE_TX','BRIDGE_RX','BRIDGE_AUTO','BRIDGE_DONGLE'):
            command=['cc','-shared','-fPIC','-std=gnu11','-Wall','-Wextra','-Werror','-I',str(w),'-I',str(PLATFORM),'-I',str(OBERON),'-I',str(OBERON/'include'),'-DNRF5_ESB_'+('RX' if role.endswith('DONGLE') else 'TX' if role.endswith('AUTO') else role.removeprefix('BRIDGE_'))]
            if role.startswith('BRIDGE_'): command+=['-DNRF5_ESB_BRIDGE_ENABLE','-DFIXTURE_BRIDGE',str(PLATFORM/'esb_bridge.c')]
            if role.endswith('DONGLE'): command+=['-DNRF5_ESB_DONGLE']
            if role.endswith('AUTO'): command+=['-DFIXTURE_ROUTING','-DNRF5_ESB_ENABLE','-I',str(PLATFORM.parents[1]/'tmk_core/protocol/nrf5')]
            if role in ('TX','BRIDGE_TX'): command+=['-DNRF5_ESB_PAIR_COMBO={{4,0},{4,10}}']
            command += [str(w/'fixture.c'),str(w/'crypto.c'),str(PLATFORM/'esb_crypto.c'),str(PLATFORM/'esb_packet.c'),str(PLATFORM/'esb_pair_crypto.c'),'-o',str(w/f'{role}.so')]
            subprocess.run(command,check=True)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def setUp(self):
        # Each scenario gets independent static state and a distinct loader path.
        self.scenario=tempfile.TemporaryDirectory();self.addCleanup(self.scenario.cleanup)
        self.nodes=[self.load_node(role,i) for i,role in enumerate(('TX','RX'))]
        self.tx,self.rx=self.nodes;self.clock=0;self.frames=[];self.rx_online=True

    def load_node(self,role,i):
        path=Path(self.scenario.name)/f'{role}.so';shutil.copy2(self.work/f'{role}.so',path)
        lib=C.CDLL(str(path));lib.fixture_backend.argtypes=[self.callback_type];lib.fixture_backend(self.backend)
        for name in ('nrf5_pair_start','nrf5_pair_confirm','nrf5_pair_cancel','nrf5_pair_busy','nrf5_esb_connected'):
            getattr(lib,name).restype=C.c_bool
        lib.nrf5_pair_state.restype=C.c_uint8;lib.nrf5_pair_fingerprint.restype=C.c_uint32
        lib.fixture_init.argtypes=[C.c_uint64];lib.fixture_init(52840+i*10000)
        lib.fixture_tick.argtypes=[C.c_uint32];lib.fixture_address.restype=C.c_uint32
        lib.fixture_accepts.argtypes=[C.c_uint32];lib.fixture_accepts.restype=C.c_bool
        lib.fixture_wire.argtypes=[B];lib.fixture_wire.restype=C.c_uint8
        lib.fixture_put.argtypes=[B,C.c_uint8];lib.fixture_reply.argtypes=[B,C.c_uint8,B];lib.fixture_reply.restype=C.c_uint8
        lib.fixture_result.argtypes=[B,C.c_uint8];lib.fixture_key.argtypes=[C.c_uint8];lib.fixture_latest.restype=C.c_uint8
        lib.fixture_saved_address.restype=C.c_uint32;lib.fixture_writes.restype=C.c_int
        lib.fixture_hold_ctrl.argtypes=[C.c_bool];lib.fixture_auto_commit.argtypes=[C.c_bool];lib.fixture_key_material.argtypes=[B]
        lib.nrf5_pair_key.argtypes=[C.c_bool]
        return lib

    def step(self,drop=False,tamper=False):
        self.clock+=10;self.rx.fixture_tick(self.clock);self.tx.fixture_tick(self.clock)
        p=(C.c_uint8*64)();n=self.tx.fixture_wire(p)
        if n:
            self.frames.append(bytes(p[:n]));reply=(C.c_uint8*64)();m=0
            if self.rx_online and self.rx.fixture_accepts(self.tx.fixture_address()):
                self.rx.fixture_put(p,n);self.rx.fixture_tick(self.clock)
                m=self.rx.fixture_reply(p,n,reply)
            if tamper and m==64 and reply[0]==0xA2: reply[63]^=1
            self.tx.fixture_result(reply,0 if drop else m)

    def advance(self,count=100):
        for _ in range(count):self.step()

    def bridge_nodes(self):
        self.tx=self.load_node('BRIDGE_TX',0);self.rx=self.load_node('BRIDGE_RX',1)
        for node in (self.tx,self.rx):
            node.fixture_bridge_push.argtypes=[C.c_uint8,C.c_uint16,B,C.c_uint8]
            node.fixture_bridge_push.restype=C.c_bool
            node.fixture_bridge_events.argtypes=[C.c_uint8];node.fixture_bridge_events.restype=C.c_uint
            node.fixture_bridge_last.argtypes=[B];node.fixture_bridge_last.restype=C.c_uint8
            node.fixture_bridge_token.restype=C.c_uint16
            node.fixture_bridge_block.argtypes=[C.c_bool]
        self.advance(40)
        self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(self.rx.nrf5_esb_connected())

    def bridge_push(self,node,kind,payload,token=0):
        data=(C.c_uint8*len(payload)).from_buffer_copy(bytes(payload))
        self.assertTrue(node.fixture_bridge_push(kind,token,data,len(payload)))

    def test_bridge_via_control_music_midi_and_typing_survive_dropped_acks(self):
        self.bridge_nodes()
        via=bytes([4])+bytes(range(1,32))
        self.bridge_push(self.rx,9,via,0x1234)
        self.bridge_push(self.tx,3,bytes([9,0x90,60,100]))
        self.bridge_push(self.tx,3,bytes([8,0x80,60,0]))
        self.bridge_push(self.tx,2,bytes([0xc7,0x83,17])+bytes(17))
        self.tx.fixture_key(4)
        for i in range(160):self.step(drop=i%7==0)
        self.assertEqual(self.rx.fixture_latest(),4)
        self.assertEqual(self.rx.fixture_bridge_events(1),1)
        self.assertEqual(self.rx.fixture_bridge_token(),0x1234)
        self.assertEqual(self.rx.fixture_bridge_events(2),1)
        self.assertEqual(self.rx.fixture_bridge_events(3),2)
        output=(C.c_uint8*32)();n=self.rx.fixture_bridge_last(output)
        self.assertEqual(bytes(output[:n]),via)
        self.assertEqual(self.tx.fixture_bridge_events(9),2)
        self.bridge_push(self.rx,11,bytes([9,0x90,64,80]))
        self.advance(40);self.assertEqual(self.tx.fixture_bridge_events(11),1)
        for i in range(100):self.bridge_push(self.rx,10,bytes([0xc7,1,i,3])+bytes([i])*16)
        self.tx.fixture_key(0);self.advance(8);self.assertEqual(self.rx.fixture_latest(),0)
        self.advance(50);self.assertEqual(self.tx.fixture_bridge_events(10),1)
        n=self.tx.fixture_bridge_last(output);self.assertEqual(bytes(output[:n]),bytes([0xc7,1,99,3])+bytes([99])*16)

    def test_bridge_auto_modes_recover_without_leaking_bluetooth_or_usb_keys(self):
        self.bridge_nodes()
        self.tx=self.load_node('BRIDGE_AUTO',0)
        self.tx.fixture_mode_current.restype=C.c_uint8
        self.tx.fixture_usb.argtypes=[C.c_bool]
        self.advance(100);self.assertEqual(self.tx.fixture_mode_current(),3)
        self.tx.fixture_key(4);self.advance(10);self.assertEqual(self.rx.fixture_latest(),4)
        self.rx_online=False;self.advance(100);self.assertEqual(self.tx.fixture_mode_current(),2)
        self.tx.fixture_key(5);self.advance(20)
        self.rx_online=True;self.advance(100);self.assertEqual(self.tx.fixture_mode_current(),3)
        self.assertNotEqual(self.rx.fixture_latest(),5)
        self.tx.fixture_usb(True);self.advance(15);self.assertEqual(self.tx.fixture_mode_current(),1)
        self.tx.fixture_key(6);self.advance(10);self.assertNotEqual(self.rx.fixture_latest(),6)
        self.tx.fixture_usb(False);self.advance(120);self.assertEqual(self.tx.fixture_mode_current(),3)
        self.tx.fixture_key(7);self.advance(10);self.assertEqual(self.rx.fixture_latest(),7)

    def test_bridge_dongle_physical_pair_then_remote_via_and_midi(self):
        self.bridge_nodes();self.rx=self.load_node('BRIDGE_DONGLE',1)
        self.rx.fixture_bridge_push.argtypes=[C.c_uint8,C.c_uint16,B,C.c_uint8];self.rx.fixture_bridge_push.restype=C.c_bool
        self.rx.fixture_bridge_events.argtypes=[C.c_uint8];self.rx.fixture_bridge_events.restype=C.c_uint
        self.rx.fixture_unbind();self.rx.nrf5_pair_usb_window()
        self.tx.fixture_hold_ctrl(True);self.advance(450);self.tx.fixture_hold_ctrl(False)
        self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(self.rx.nrf5_esb_connected())
        self.bridge_push(self.rx,9,bytes(range(32)),56)
        self.bridge_push(self.tx,3,bytes([9,0x90,60,80]));self.advance(100)
        self.assertEqual(self.rx.fixture_bridge_events(1),1);self.assertEqual(self.rx.fixture_bridge_events(3),1)

    def test_bridge_pair_cancel_does_not_execute_a_stale_edit(self):
        self.bridge_nodes();self.tx.fixture_bridge_block(True)
        self.bridge_push(self.rx,9,bytes(range(32)),41);self.advance(5)
        self.assertTrue(self.tx.nrf5_pair_start());self.advance(2)
        self.assertTrue(self.tx.nrf5_pair_cancel());self.tx.fixture_bridge_block(False)
        self.advance(100);self.assertEqual(self.tx.fixture_bridge_events(9),0)
        self.bridge_push(self.rx,9,bytes(range(32)),42);self.advance(60)
        self.assertEqual(self.tx.fixture_bridge_events(9),2);self.assertEqual(self.rx.fixture_bridge_token(),42)

    def test_bridge_stop_discards_older_coalesced_music_frames(self):
        self.bridge_nodes()
        self.bridge_push(self.rx,10,bytes([0xc7,1,1,3])+bytes([50])*16)
        stop=bytes([0xc7,1,2,0])+bytes(16)
        self.bridge_push(self.rx,10,stop)
        self.advance(60)
        self.assertEqual(self.tx.fixture_bridge_events(10),1)
        output=(C.c_uint8*32)();n=self.tx.fixture_bridge_last(output)
        self.assertEqual(bytes(output[:n]),stop)

    def test_bridge_backpressure_retries_without_duplicate_edits_or_stopping_keys(self):
        self.bridge_nodes();self.tx.fixture_bridge_block(True)
        self.bridge_push(self.rx,9,bytes(range(32)),77)
        self.tx.fixture_key(4);self.advance(20)
        self.assertEqual(self.rx.fixture_latest(),4);self.assertEqual(self.tx.fixture_bridge_events(9),0)
        self.tx.fixture_bridge_block(False);self.advance(60)
        self.assertEqual(self.tx.fixture_bridge_events(9),2);self.assertEqual(self.rx.fixture_bridge_events(1),1)
        self.rx.fixture_bridge_block(True)
        for i in range(20):self.bridge_push(self.tx,3,bytes([9,0x90,i,100]))
        self.advance(50);self.tx.fixture_key(5);self.advance(8)
        self.assertEqual(self.rx.fixture_latest(),5);self.assertEqual(self.rx.fixture_bridge_events(3),0)
        self.rx.fixture_bridge_block(False);self.advance(80)
        self.assertEqual(self.rx.fixture_bridge_events(3),20)

    def test_bridge_tampered_downlink_does_not_break_keyboard_or_get_applied(self):
        self.bridge_nodes()
        self.bridge_push(self.rx,10,bytes([0xc7,1,1,3])+bytes([25])*16)
        self.tx.fixture_key(6)
        for _ in range(20):self.step(tamper=True)
        self.assertEqual(self.rx.fixture_latest(),6);self.assertEqual(self.tx.fixture_bridge_events(10),0)
        self.advance(40);self.assertEqual(self.tx.fixture_bridge_events(10),1)
        self.rx_online=False;self.advance(40)
        self.assertFalse(self.tx.nrf5_esb_connected())
        self.rx_online=True;self.advance(60)
        self.bridge_push(self.rx,10,bytes([0xc7,1,2,3])+bytes([80])*16)
        self.advance(40);self.assertEqual(self.tx.fixture_bridge_events(10),2)
        self.rx.fixture_receiver_reboot();self.advance(100)
        self.bridge_push(self.rx,10,bytes([0xc7,1,3,3])+bytes([95])*16)
        self.advance(40);self.assertEqual(self.tx.fixture_bridge_events(10),3)

    def candidate(self):
        self.assertTrue(self.rx.nrf5_pair_start());self.assertTrue(self.tx.nrf5_pair_start())
        self.advance(30);self.assertEqual(self.rx.nrf5_pair_state(),3)
        self.assertEqual(self.rx.fixture_writes(),0);self.assertEqual(self.tx.fixture_writes(),0)
        self.assertEqual(self.rx.nrf5_pair_fingerprint(),self.tx.nrf5_pair_fingerprint())

    def dongle(self):
        self.rx=self.load_node('DONGLE',1)
        return self.rx

    def test_dongle_plug_and_keyboard_combo_pair_without_client_or_receiver_controls(self):
        rx=self.dongle();rx.fixture_unbind();rx.nrf5_pair_usb_window()
        self.tx.fixture_hold_ctrl(True);self.advance(450);self.tx.fixture_hold_ctrl(False)
        self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(rx.nrf5_esb_connected())
        self.assertNotEqual(rx.fixture_saved_address(),17)
        self.assertEqual(rx.fixture_saved_address(),self.tx.fixture_saved_address())
        self.tx.fixture_key(4);self.advance(20);self.assertEqual(rx.fixture_latest(),4)

    def test_bound_dongle_discovery_keeps_existing_keyboard_working_and_rebinds_without_client(self):
        rx=self.dongle();self.advance(100);self.assertTrue(rx.nrf5_esb_connected())
        rx.nrf5_pair_usb_window();self.advance(3)
        self.assertFalse(rx.nrf5_pair_busy());self.assertEqual(rx.nrf5_pair_state(),1)
        self.tx.fixture_key(4);self.advance(20);self.assertEqual(rx.fixture_latest(),4)
        self.tx.fixture_key(0);self.advance(20)
        self.tx.fixture_hold_ctrl(True);self.advance(450);self.tx.fixture_hold_ctrl(False)
        self.assertTrue(rx.nrf5_esb_connected());self.assertNotEqual(rx.fixture_saved_address(),17)

    def test_dongle_closed_window_rejects_unsolicited_pairing_and_keeps_old_binding(self):
        rx=self.dongle();rx.nrf5_pair_usb_window();self.advance(10)
        self.clock+=60010;self.advance(3);self.assertFalse(rx.nrf5_pair_busy())
        self.tx.fixture_hold_ctrl(True);self.advance(450);self.tx.fixture_hold_ctrl(False)
        self.assertEqual(rx.fixture_writes(),0);self.assertEqual(rx.fixture_saved_address(),17)

    def test_unbound_dongle_window_expires_and_can_be_reopened_by_usb_insertion(self):
        rx=self.dongle();rx.fixture_unbind();rx.nrf5_pair_usb_window()
        self.clock+=60010;self.advance(3);self.assertEqual(rx.nrf5_pair_state(),7)
        self.assertEqual(rx.fixture_writes(),0)
        rx.nrf5_pair_usb_window();self.tx.fixture_hold_ctrl(True);self.advance(450)
        self.assertTrue(rx.nrf5_esb_connected())

    def test_dongle_pairing_survives_rtc_counter_rollover(self):
        rx=self.dongle();rx.fixture_unbind()
        self.clock=524000;self.advance(1);rx.nrf5_pair_usb_window()
        self.tx.fixture_hold_ctrl(True);self.advance(450)
        self.assertTrue(rx.nrf5_esb_connected());self.assertTrue(self.tx.nrf5_esb_connected())
        self.assertEqual(rx.fixture_saved_address(),self.tx.fixture_saved_address())

    def test_auto_dongle_preserves_save_order_and_recovers_lost_done(self):
        rx=self.dongle();rx.fixture_auto_commit(False);rx.nrf5_pair_usb_window()
        self.assertTrue(self.tx.nrf5_pair_start());self.advance(80)
        self.assertEqual(rx.nrf5_pair_state(),4);self.assertEqual(self.tx.fixture_writes(),0)
        rx.fixture_auto_commit(True)
        for _ in range(40):self.step(drop=True)
        self.advance(150);self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(rx.nrf5_esb_connected())

    def test_auto_routing_falls_back_to_ble_and_rediscovers_without_leaking_keys(self):
        self.tx=self.load_node('AUTO',0)
        self.tx.fixture_mode(0);self.advance(120)
        self.assertEqual(self.tx.fixture_mode_current(),3)
        self.tx.fixture_key(4);self.advance(20);self.assertEqual(self.rx.fixture_latest(),4)
        self.rx_online=False;self.advance(100)
        self.assertEqual(self.tx.fixture_mode_current(),2)
        self.assertEqual(self.rx.fixture_latest(),0) # receiver releases lost-link keys
        before=len(self.frames);self.tx.fixture_key(5);self.advance(200)
        self.assertEqual(self.tx.fixture_ble_key(),5)
        probes=self.frames[before:]
        self.assertTrue(probes);self.assertTrue(all(p[2]==6 for p in probes)) # HELLO only
        self.assertLessEqual(len(probes),18) # bounded bursts with backoff
        self.assertTrue(any(a==b for a,b in zip(probes,probes[1:]))) # retry immutable packet
        self.assertGreater(len({p[4:16] for p in probes}),1) # fresh later probes
        adverts=self.tx.fixture_advertisements();self.advance(200)
        self.assertEqual(self.tx.fixture_advertisements(),adverts) # no advertising loop
        self.rx.fixture_receiver_reboot();self.rx_online=True;self.advance(200)
        self.assertEqual(self.tx.fixture_mode_current(),3)
        self.assertEqual(self.tx.fixture_ble_key(),0) # release the previous host
        self.assertEqual(self.rx.fixture_latest(),0) # BLE key was never forwarded
        self.tx.fixture_key(6);self.advance(30);self.assertEqual(self.rx.fixture_latest(),6)
        self.tx.fixture_usb(True);self.advance(40)
        self.assertEqual(self.tx.fixture_mode_current(),1)
        self.tx.fixture_key(7);self.advance(30);self.assertEqual(self.tx.fixture_usb_key(),7)
        self.tx.fixture_usb(False);self.advance(200)
        self.assertEqual(self.tx.fixture_mode_current(),3)
        self.assertEqual(self.tx.fixture_usb_key(),0)
        self.assertEqual(self.rx.fixture_latest(),0)

    def test_auto_start_without_receiver_uses_ble_and_manual_modes_stop_probing(self):
        self.tx=self.load_node('AUTO',0);self.rx_online=False
        self.tx.fixture_mode(0);self.advance(100)
        self.assertEqual(self.tx.fixture_mode_current(),2)
        self.tx.fixture_mode(2);self.advance(80)
        before=len(self.frames);self.rx_online=True;self.advance(200)
        self.assertEqual(self.tx.fixture_mode_current(),2)
        self.assertEqual(len(self.frames),before)
        self.tx.fixture_mode(3);self.advance(150)
        self.assertEqual(self.tx.fixture_mode_current(),3)
        self.rx_online=False;self.advance(150)
        self.assertEqual(self.tx.fixture_mode_current(),3) # explicit RF stays fixed

    def test_repair_preserves_auto_mode_and_still_falls_back_after_success(self):
        self.tx=self.load_node('AUTO',0);self.rx=self.load_node('DONGLE',1)
        self.tx.fixture_mode(0);self.advance(40)
        self.rx.fixture_unbind();self.rx.nrf5_pair_usb_window()
        self.assertTrue(self.tx.nrf5_pair_start());self.advance(200)
        self.assertEqual(self.tx.fixture_mode_preferred(),0)
        self.assertEqual(self.tx.fixture_mode_current(),3)
        self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(self.rx.nrf5_esb_connected())
        self.rx_online=False;self.advance(100)
        self.assertEqual(self.tx.fixture_mode_preferred(),0)
        self.assertEqual(self.tx.fixture_mode_current(),2)

    def test_host_caps_changes_propagate_through_authenticated_radio_acks(self):
        self.advance(100)
        for state in (0,2,0,3,2,0):
            self.rx.fixture_host_leds(state);self.advance(30)
            self.assertEqual(self.tx.fixture_rf_leds(),state)
        self.rx_online=False;self.advance(80)
        self.assertEqual(self.tx.fixture_rf_leds(),0)
        self.rx.fixture_host_leds(2);self.rx_online=True;self.advance(100)
        self.assertEqual(self.tx.fixture_rf_leds(),2)

    def test_wireless_confirmation_save_then_encrypted_typing(self):
        self.candidate();self.assertTrue(self.rx.nrf5_pair_confirm());self.advance(100)
        self.assertEqual(self.tx.nrf5_pair_state(),5);self.assertEqual(self.rx.nrf5_pair_state(),5)
        self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(self.rx.nrf5_esb_connected())
        self.assertNotEqual(self.tx.fixture_saved_address(),17)
        self.assertEqual(self.tx.fixture_saved_address(),self.rx.fixture_saved_address())
        a=(C.c_uint8*16)();b=(C.c_uint8*16)();self.tx.fixture_key_material(a);self.rx.fixture_key_material(b)
        self.assertEqual(bytes(a),bytes(b));self.assertFalse(any(bytes(a) in frame for frame in self.frames))
        self.tx.fixture_key(4);self.advance(30);self.assertEqual(self.rx.fixture_latest(),4)
        self.tx.fixture_key(0);self.advance(20);self.assertEqual(self.rx.fixture_latest(),0)

    def test_receiver_api_and_keyboard_combo_pair_without_keyboard_usb(self):
        # The dedicated receiver has no physical input: the USB management
        # command opens/confirms its window; only the transmitter scans keys.
        self.assertTrue(self.rx.nrf5_pair_start())
        self.tx.fixture_hold_ctrl(True);self.advance(340);self.tx.fixture_hold_ctrl(False)
        self.assertEqual(self.rx.nrf5_pair_state(),3);self.assertEqual(self.rx.fixture_writes(),0)
        self.assertTrue(self.rx.nrf5_pair_confirm());self.advance(100)
        self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(self.rx.nrf5_esb_connected())

    def test_cancel_or_timeout_does_not_replace_old_pair(self):
        self.candidate();self.assertTrue(self.rx.nrf5_pair_cancel());self.advance(3)
        self.assertEqual(self.rx.fixture_saved_address(),17);self.assertEqual(self.rx.fixture_writes(),0)
        self.clock+=60010;self.advance(3)
        self.assertEqual(self.tx.nrf5_pair_state(),7);self.assertEqual(self.tx.fixture_writes(),0)

    def test_lost_done_recovers_on_discovery_pipe_after_receiver_saved(self):
        self.candidate();self.assertTrue(self.rx.nrf5_pair_confirm())
        for _ in range(40):self.step(drop=True)
        self.assertEqual(self.rx.nrf5_pair_state(),5);self.assertNotEqual(self.rx.fixture_saved_address(),17)
        self.assertEqual(self.tx.fixture_saved_address(),17)
        self.advance(120);self.assertTrue(self.tx.nrf5_esb_connected());self.assertTrue(self.rx.nrf5_esb_connected())

    def test_transmitter_saves_only_after_receiver_flash_completed(self):
        self.candidate();self.rx.fixture_auto_commit(False);self.assertTrue(self.rx.nrf5_pair_confirm());self.advance(60)
        self.assertEqual(self.rx.nrf5_pair_state(),4);self.assertEqual(self.tx.fixture_writes(),0)
        self.assertFalse(self.rx.nrf5_pair_cancel())
        self.rx.fixture_auto_commit(True);self.advance(100);self.assertEqual(self.tx.nrf5_pair_state(),5)

    def test_physical_combo_requires_three_seconds_and_works_without_via_mapping(self):
        self.tx.fixture_hold_ctrl(True);self.advance(200);self.assertEqual(self.tx.nrf5_pair_state(),0)
        self.advance(110);self.assertEqual(self.tx.nrf5_pair_state(),1)
        self.assertTrue(self.tx.nrf5_pair_cancel());self.advance(3)
        self.tx.fixture_hold_ctrl(False);self.advance(2)
        self.tx.fixture_hold_ctrl(True);self.advance(310);self.assertEqual(self.tx.nrf5_pair_state(),1)

    def test_invalid_peer_point_cannot_become_confirmable(self):
        self.assertTrue(self.rx.nrf5_pair_start());self.advance(10)
        p=(C.c_uint8*64)();p[0]=0x51;p[1]=3;p[2]=7;p[3]=32;p[4]=1;p[12]=1
        self.rx.fixture_put(p,56);self.rx.fixture_tick(self.clock)
        self.assertEqual(self.rx.nrf5_pair_state(),1);self.assertFalse(self.rx.nrf5_pair_confirm());self.assertEqual(self.rx.fixture_writes(),0)

    def test_confirmation_at_window_end_finishes_saving_after_deadline(self):
        self.candidate()
        self.clock=30000;self.advance(4) # keep the authenticated peer alive
        self.clock=59980;self.step()
        self.assertTrue(self.rx.nrf5_pair_confirm());self.advance(100)
        self.assertEqual(self.tx.nrf5_pair_state(),5);self.assertTrue(self.tx.nrf5_esb_connected())

    def test_tampered_finish_is_not_confirmable(self):
        self.assertTrue(self.rx.nrf5_pair_start());self.assertTrue(self.tx.nrf5_pair_start())
        for _ in range(30):
            self.step()
            if self.rx.nrf5_pair_state()==2:break
        self.assertEqual(self.rx.nrf5_pair_state(),2)
        # Construct a FINISH carrying no key proof. The candidate stays gated.
        pub=next(p for p in self.frames if p[2]==7)
        bad=(C.c_uint8*64)();bad[:16]=pub[:16];bad[2]=9;bad[3]=0;bad[12]=2
        self.rx.fixture_put(bad,24);self.rx.fixture_tick(self.clock)
        self.assertFalse(self.rx.nrf5_pair_confirm());self.assertEqual(self.rx.fixture_writes(),0)
