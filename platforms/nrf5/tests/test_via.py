# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production VIA router with ASan/UBSan; SDK calls are stubs."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]

class ViaTests(unittest.TestCase):
    def test_queue_ble_fragments_response_routing_and_disconnect(self):
        if not shutil.which('cc'):
            self.skipTest('Native C compiler is required')
        with tempfile.TemporaryDirectory() as d:
            work = Path(d)
            (work/'app_util_platform.h').write_text('#define CRITICAL_REGION_ENTER() do {} while(0)\n#define CRITICAL_REGION_EXIT() do {} while(0)\n')
            (work/'raw_hid.h').write_text('#include <stdint.h>\nvoid raw_hid_receive(uint8_t *, uint8_t);\n')
            (work/'test.c').write_text(r'''
#include "via_transport.h"
#include <assert.h>
#include <string.h>
static int processed, usb, ble;
static bool ready = true;
static uint8_t result[32];
static uint16_t last_sequence;
bool nrf5_eeprom_pending(void) { return false; }
uint32_t nrf5_eeprom_last_error(void) { return 0; }
void raw_hid_receive(uint8_t *data, uint8_t len) { processed++; nrf_via_send(data,len); }
bool nrf_via_usb_reply(const uint8_t *data) { if(!ready)return false; memcpy(result,data,32);usb++;return true; }
bool nrf_via_ble_reply(const uint8_t *data, uint16_t seq) { if(!ready)return false;memcpy(result,data,32);last_sequence=seq;ble++;return true; }
static void run(void) { for(int i=0;i<20;i++)nrf_via_task(); }
int main(void) {
    uint8_t request[32]={1};
    assert(!nrf_via_receive(request,31,0,0)); assert(!nrf_via_receive(request,32,2,0));
    for(int i=0;i<4;i++)assert(nrf_via_receive(request,32,i%2,100+i));
    assert(!nrf_via_receive(request,32,0,0));
    assert(processed==0);run();assert(processed==4&&usb==2&&ble==2&&last_sequence==103);
    ready=false;assert(nrf_via_receive(request,32,0,0));run();assert(processed==5&&usb==2);
    assert(nrf_via_receive(request,32,1,700));run();assert(processed==5);
    nrf_via_disconnect(0);run();assert(processed==6&&ble==2);
    ready=true;run();assert(ble==3&&last_sequence==700&&usb==2);
    // SDK disconnect drops old queued commands while preserving the other host.
    assert(nrf_via_receive(request,32,1,701));assert(nrf_via_receive(request,32,0,0));nrf_via_disconnect(1);run();assert(processed==7&&usb==3);
    uint8_t a[19]={0x34,0x12,0}, b[19]={0x34,0x12,16};a[3]=1;
    nrf_via_ble_fragment(b,19);run();assert(processed==7);
    nrf_via_ble_fragment(a,19);b[0]++;nrf_via_ble_fragment(b,19);run();assert(processed==7);b[0]--;
    nrf_via_ble_fragment(a,19);nrf_via_disconnect(1);nrf_via_ble_fragment(b,19);run();assert(processed==7);
    nrf_via_ble_fragment(a,19);nrf_via_ble_fragment(b,18);nrf_via_ble_fragment(b,19);run();assert(processed==7);
    nrf_via_ble_fragment(a,19);nrf_via_ble_fragment(b,19);run();assert(processed==8&&ble==4&&last_sequence==0x1234);
    // A malformed buffer size must never reach VIA's unchecked output buffer.
    request[0]=0x12;request[3]=255;assert(nrf_via_receive(request,32,0,0));run();assert(processed==8&&result[0]==255);
    request[0]=5;request[1]=7;request[3]=0;assert(nrf_via_receive(request,32,0,0));run();assert(processed==8&&result[0]==255);
    request[0]=0x80;assert(nrf_via_receive(request,32,0,0));run();assert(processed==8&&result[0]==0x80&&result[1]==0);
    request[0]=0x81;assert(nrf_via_receive(request,32,0,0));run();assert(processed==8&&result[0]==0x81&&result[1]==1&&result[2]==5&&result[3]==14);
    // app_usbd_hid_generic_out_report_get returns capacity 33 on both paths.
    int prior=usb; int prior_processed=processed;
    uint8_t interrupt[33]={1,0xA5};interrupt[32]=0xCC;
    assert(nrf_via_usb_receive(interrupt,sizeof(interrupt)));run();
    assert(usb==prior+1&&processed==prior_processed+1&&result[0]==1&&result[1]==0xA5&&result[31]==0);
    uint8_t control[33]={0,1,0xB6};control[32]=0xD7;
    assert(nrf_via_usb_receive(control,sizeof(control)));run();
    assert(usb==prior+2&&processed==prior_processed+2&&result[0]==1&&result[1]==0xB6&&result[31]==0xD7);
    assert(!nrf_via_usb_receive(control,31));assert(!nrf_via_usb_receive(control,34));assert(!nrf_via_usb_receive(NULL,33));
    assert(!memcmp(interrupt,(uint8_t[]){1,0xA5},2));assert(control[0]==0&&control[1]==1); // Never mutate SDK DMA memory.
    return 0;
}
''')
            binary = work/'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',
                '-DVIA_ENABLE', '-DDYNAMIC_KEYMAP_LAYER_COUNT=7','-DMATRIX_ROWS=5','-DMATRIX_COLS=14',
                '-I',d,'-I',str(ROOT/'platforms/nrf5'),str(ROOT/'platforms/nrf5/via_transport.c'),str(work/'test.c'),'-o',str(binary)],check=True,capture_output=True,text=True)
            subprocess.run([str(binary)],check=True,capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
