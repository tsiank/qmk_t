# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare firmware CCM/CMAC against an independent cryptography implementation.
Only the Nordic ECB primitive is replaced; production framing/mode code runs in C.
"""
import ctypes as C
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3] / 'platforms/nrf5'

class Report(C.Structure):
    _fields_ = [('kind', C.c_uint8), ('length', C.c_uint8), ('data', C.c_uint8 * 32)]
class Ecb(C.Structure):
    _fields_ = [('key', C.c_uint8 * 16), ('cleartext', C.c_uint8 * 16), ('ciphertext', C.c_uint8 * 16)]

class EsbCryptoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
            from cryptography.hazmat.primitives.ciphers.aead import AESCCM
            from cryptography.hazmat.primitives.cmac import CMAC
        except ImportError:
            raise unittest.SkipTest('Python cryptography required for independent vectors')
        if not shutil.which('cc'):
            raise unittest.SkipTest('Native C compiler required')
        cls.Cipher, cls.algorithms, cls.modes, cls.AESCCM, cls.CMAC = Cipher, algorithms, modes, AESCCM, CMAC
        cls.temp = tempfile.TemporaryDirectory(); work = Path(cls.temp.name)
        (work/'nrf_soc.h').write_text("""#include <stdint.h>
#define NRF_SUCCESS 0
typedef struct {uint8_t key[16],cleartext[16],ciphertext[16];} nrf_ecb_hal_data_t;
uint32_t sd_ecb_block_encrypt(nrf_ecb_hal_data_t *);
""")
        (work/'backend.c').write_text("""#include "nrf_soc.h"
static uint32_t (*backend)(nrf_ecb_hal_data_t *);
void set_backend(uint32_t (*fn)(nrf_ecb_hal_data_t *)){backend=fn;}
uint32_t sd_ecb_block_encrypt(nrf_ecb_hal_data_t *p){return backend(p);}
""")
        subprocess.run(['cc','-shared','-fPIC','-std=gnu11','-Wall','-Wextra','-Werror','-I',str(work),'-I',str(ROOT),
            str(ROOT/'esb_crypto.c'),str(ROOT/'esb_packet.c'),str(work/'backend.c'),'-o',str(work/'crypto.so')],check=True)
        cls.lib=C.CDLL(str(work/'crypto.so'))
        callback=C.CFUNCTYPE(C.c_uint32,C.POINTER(Ecb))
        def encrypt(block):
            b=block.contents
            cipher=Cipher(algorithms.AES(bytes(b.key)),modes.ECB()).encryptor()
            result=cipher.update(bytes(b.cleartext))+cipher.finalize()
            b.ciphertext[:]=result
            return 0
        cls.callback=callback(encrypt)
        cls.lib.set_backend.argtypes=[callback];cls.lib.set_backend(cls.callback)
        byteptr=C.POINTER(C.c_uint8)
        cls.lib.nrf5_esb_encode.argtypes=[byteptr,C.c_uint64,C.c_uint32,C.POINTER(Report)]
        cls.lib.nrf5_esb_encode.restype=C.c_uint8
        for name in ('nrf5_esb_seal','nrf5_esb_open'):
            fn=getattr(cls.lib,name);fn.argtypes=[byteptr,C.c_uint64,byteptr,C.c_uint8];fn.restype=C.c_bool
        cls.lib.nrf5_esb_ack_seal.argtypes=[byteptr,byteptr,C.c_uint64,C.c_uint8,C.c_bool,byteptr]
        cls.lib.nrf5_esb_ack_seal.restype=C.c_bool
        cls.lib.nrf5_esb_ack_open.argtypes=[byteptr,byteptr,byteptr,C.POINTER(C.c_uint64),C.POINTER(C.c_uint8),C.POINTER(C.c_bool)]
        cls.lib.nrf5_esb_ack_open.restype=C.c_bool

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def aes(self,key,block):
        cipher=self.Cipher(self.algorithms.AES(key),self.modes.ECB()).encryptor()
        return cipher.update(block)+cipher.finalize()

    def test_ccm_against_independent_vectors_and_tampering(self):
        rng=random.Random(52840)
        for kind,size in ((1,8),(2,31),(3,3),(4,5),(6,0),(7,32),(8,4),(9,20),(10,20),(11,4)):
            for sample in range(12):
                key=rng.randbytes(16); challenge=rng.getrandbits(64)
                session=rng.getrandbits(64);seq=rng.getrandbits(32)
                plain=rng.randbytes(size)
                if kind==3: plain=bytes([3])+plain[1:]
                r=Report(kind,size);r.data[:size]=plain
                p=(C.c_uint8*64)();k=(C.c_uint8*16).from_buffer_copy(key)
                n=self.lib.nrf5_esb_encode(p,session,seq,C.byref(r));self.assertEqual(n,24+size)
                header=bytes(p[:16]); hello=kind==6
                if hello: derived=self.aes(key,b'Qmknrf5 HELLOv2'.ljust(16,b'\0'))
                else:
                    root=self.aes(key,(b'Qmknrf5 DOWN v1' if kind>=9 else b'Qmknrf5 DATA v2').ljust(16,b'\0'))
                    derived=self.aes(root,session.to_bytes(8,'little')+challenge.to_bytes(8,'little'))
                nonce=(session^(0 if hello else challenge)).to_bytes(8,'little')+seq.to_bytes(4,'little')+bytes([2 if hello else 1 if kind>=9 else 0])
                expected=self.AESCCM(derived,tag_length=8).encrypt(nonce,plain,header)
                self.assertTrue(self.lib.nrf5_esb_seal(k,challenge,p,n))
                self.assertEqual(bytes(p[16:n]),expected)
                wire=bytes(p)
                for pos in (2,4,12,n-1):
                    bad=(C.c_uint8*64).from_buffer_copy(wire);bad[pos]^=1
                    self.assertFalse(self.lib.nrf5_esb_open(k,challenge,bad,n))
                self.assertTrue(self.lib.nrf5_esb_open(k,challenge,p,n))
                self.assertEqual(bytes(p[16:16+size]),plain)
                if not hello:
                    wrong=(C.c_uint8*64).from_buffer_copy(wire)
                    self.assertFalse(self.lib.nrf5_esb_open(k,challenge^1,wrong,n))

    def test_authenticated_ack_matches_cmac_and_rejects_wrong_session_or_key(self):
        key=bytes(range(16));k=(C.c_uint8*16).from_buffer_copy(key)
        p=(C.c_uint8*64)();r=Report(1,8)
        self.lib.nrf5_esb_encode(p,0x123456789ABCDEF0,1234,C.byref(r))
        ack=(C.c_uint8*30)();challenge=0x9876543212345678
        self.assertTrue(self.lib.nrf5_esb_ack_seal(k,p,challenge,2,True,ack))
        domain=b'Qmknrf5 ACK v2'.ljust(16,b'\0')
        cmac=self.CMAC(self.algorithms.AES(self.aes(key,domain)))
        cmac.update(domain+bytes(ack[:22]))
        self.assertEqual(bytes(ack[22:]),cmac.finalize()[:8])
        c=C.c_uint64();led=C.c_uint8();accepted=C.c_bool()
        self.assertTrue(self.lib.nrf5_esb_ack_open(k,p,ack,C.byref(c),C.byref(led),C.byref(accepted)))
        self.assertEqual(c.value,challenge);self.assertEqual(led.value,2);self.assertTrue(accepted.value)
        for pos in range(30):
            bad=(C.c_uint8*30).from_buffer_copy(ack);bad[pos]^=1
            self.assertFalse(self.lib.nrf5_esb_ack_open(k,p,bad,C.byref(c),C.byref(led),C.byref(accepted)))

    def test_two_real_transport_instances_handshake_replay_restart_and_wrong_key(self):
        from test_esb import HOST
        work=Path(self.temp.name)
        (work/'host_driver.h').write_text(HOST)
        (work/'timer.h').write_text('#include <stdint.h>\nuint32_t timer_read32(void);')
        (work/'nrf5_usb.h').write_text('#include "host_driver.h"\nhost_driver_t *nrf5_usb_driver(void);\nbool nrf5_usb_esb_report(uint8_t,const uint8_t *,uint8_t);')
        with (work/'nrf_soc.h').open('a') as f:
            f.write('uint32_t sd_rand_application_vector_get(uint8_t *,uint8_t);\n')
        helper=r'''
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

static nrf5_esb_settings_t config={.paired=1,.address=17,.key={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
static uint32_t now;
static uint8_t input[64],input_length,wire[64],wire_length,cached_packet[64],cached_length,cached_ack[30],received_ack[30];
static bool ready,result_ack;
static uint8_t latest_key;
static uint64_t random_value=0x123456789ABCDE;
uint32_t timer_read32(void){return now;}
uint32_t sd_rand_application_vector_get(uint8_t *p,uint8_t n){random_value++;memcpy(p,&random_value,n);return 0;}
void nrf5_esb_settings_init(void){}
void nrf5_esb_settings_task(void){}
const nrf5_esb_settings_t *nrf5_esb_settings(void){return &config;}
bool nrf5_esb_settings_pending(void){return false;}
uint32_t nrf5_esb_settings_error(void){return 0;}
void nrf5_esb_radio_init(void){}
void nrf5_esb_radio_task(void){}
void nrf5_esb_radio_configure(bool a,uint32_t b){(void)a;(void)b;}
uint32_t nrf5_esb_radio_error(void){return 0;}
void nrf5_esb_radio_counters(uint32_t c[5]){memset(c,0,20);}
bool nrf5_esb_radio_send(const uint8_t *p,uint8_t n){memcpy(wire,p,n);wire_length=n;return true;}
bool nrf5_esb_radio_result(bool *a){if(!ready)return false;*a=result_ack;ready=false;wire_length=0;return true;}
void nrf5_esb_radio_ack(uint8_t a[30]){memcpy(a,received_ack,30);}
bool nrf5_esb_radio_receive(uint8_t *p,uint8_t *n){if(!input_length)return false;memcpy(p,input,input_length);*n=input_length;input_length=0;return true;}
void nrf5_esb_radio_cache_ack(const uint8_t *p,uint8_t n,const uint8_t a[30]){memcpy(cached_packet,p,n);cached_length=n;memcpy(cached_ack,a,30);}
static uint8_t usb_leds(void){return 2;}
static host_driver_t usb={.keyboard_leds=usb_leds};
host_driver_t *nrf5_usb_driver(void){return &usb;}
bool nrf5_usb_esb_report(uint8_t kind,const uint8_t *p,uint8_t n){(void)n;if(kind==NRF5_ESB_KEYBOARD)latest_key=p[2];return true;}
void fixture_tick(uint32_t t){now=t;nrf5_esb_task();}
void fixture_put(const uint8_t *p,uint8_t n){memcpy(input,p,n);input_length=n;}
uint8_t fixture_wire(uint8_t *p){memcpy(p,wire,wire_length);return wire_length;}
uint8_t fixture_ack(uint8_t *p){memcpy(p,cached_ack,30);return cached_length?30:0;}
void fixture_result(const uint8_t *p){memcpy(received_ack,p,30);result_ack=true;ready=true;}
void fixture_key(uint8_t key){report_keyboard_t r={0};r.keys[0]=key;driver.send_keyboard(&r);}
uint8_t fixture_latest(void){return latest_key;}
void fixture_bad_key(void){config.key[0]^=0xFF;}
void fixture_restart(void){
 head=count=0;challenge=0;connected=false;cached_length=0;
#ifdef NRF5_ESB_RX
 peer_session=0;have_peer=false;
#endif
}
'''
        libraries=[]
        for role in ('TX','RX'):
            (work/f'link_{role}.c').write_text(helper)
            subprocess.run(['cc','-shared','-fPIC','-std=gnu11','-Wall','-Wextra','-Werror','-I',str(work),'-I',str(ROOT),
                '-DNRF5_ESB_'+role,str(work/f'link_{role}.c'),str(ROOT/'esb_crypto.c'),str(ROOT/'esb_packet.c'),
                str(work/'backend.c'),'-o',str(work/f'link_{role}.so')],check=True)
            lib=C.CDLL(str(work/f'link_{role}.so'));lib.set_backend.argtypes=[type(self.callback)];lib.set_backend(self.callback)
            lib.fixture_tick.argtypes=[C.c_uint32];lib.fixture_put.argtypes=[C.POINTER(C.c_uint8),C.c_uint8]
            lib.fixture_wire.argtypes=[C.POINTER(C.c_uint8)];lib.fixture_wire.restype=C.c_uint8
            lib.fixture_ack.argtypes=[C.POINTER(C.c_uint8)];lib.fixture_ack.restype=C.c_uint8
            lib.fixture_result.argtypes=[C.POINTER(C.c_uint8)];lib.fixture_key.argtypes=[C.c_uint8]
            lib.fixture_latest.restype=C.c_uint8;lib.nrf5_esb_connected.restype=C.c_bool
            lib.nrf5_esb_set_active.argtypes=[C.c_bool];lib.nrf5_esb_init();libraries.append(lib)
        tx,rx=libraries;tx.nrf5_esb_set_active(True)
        clock=0;packets=[]
        def transfer():
            nonlocal clock
            clock+=2;tx.fixture_tick(clock)
            p=(C.c_uint8*64)();n=tx.fixture_wire(p)
            if n:
                packets.append(bytes(p[:n]));rx.fixture_put(p,n);rx.fixture_tick(clock)
                ack=(C.c_uint8*30)()
                if rx.fixture_ack(ack):tx.fixture_result(ack)
            else:rx.fixture_tick(clock)
            return n
        def settle(n=20):
            for _ in range(n):transfer()
        settle();self.assertTrue(tx.nrf5_esb_connected());self.assertTrue(rx.nrf5_esb_connected())
        tx.fixture_key(4);settle();self.assertEqual(rx.fixture_latest(),4)
        pressed=next(p for p in reversed(packets) if p[2]==1)
        tx.fixture_key(0);settle();self.assertEqual(rx.fixture_latest(),0)
        old=(C.c_uint8*64).from_buffer_copy(pressed.ljust(64,b'\0'))
        rx.fixture_put(old,len(pressed));rx.fixture_tick(clock);self.assertEqual(rx.fixture_latest(),0)
        # A receiver restart generates a fresh challenge; TX must HELLO again.
        rx.fixture_restart();tx.fixture_key(5);settle(35);self.assertEqual(rx.fixture_latest(),5)
        tx.fixture_key(0);settle();rx.fixture_put(old,len(pressed));rx.fixture_tick(clock)
        self.assertEqual(rx.fixture_latest(),0) # ciphertext from the previous challenge
        rx.fixture_bad_key();tx.fixture_key(6);settle(35)
        self.assertEqual(rx.fixture_latest(),0)
        clock+=301;tx.fixture_tick(clock);rx.fixture_tick(clock)
        self.assertFalse(tx.nrf5_esb_connected());self.assertFalse(rx.nrf5_esb_connected())
