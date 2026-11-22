# SPDX-License-Identifier: GPL-2.0-or-later
"""Native production UI protocol/render/encoder integration with memory sanitizers."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
class UiTests(unittest.TestCase):
    def test_atomic_upload_ownership_persistence_render_and_encoder(self):
        if not shutil.which('cc'): self.skipTest('cc required')
        with tempfile.TemporaryDirectory() as d:
            w=Path(d)
            files={
'quantum.h':r'''#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define KC_NO 0
#define KC_VOLU 128
#define KC_VOLD 129
#define DYNAMIC_KEYMAP_LAYER_COUNT 5
#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 2047
#define NRF5_UI_EEPROM_ADDR 2048
#define EEPROM_SIZE 3840
#define FDS_VIRTUAL_PAGES 5
extern uint32_t layer_state,default_layer_state;
uint8_t get_highest_layer(uint32_t);
bool encoder_update_user(uint8_t,bool);
void tap_code16(uint16_t);
#define TAP_CODE_DELAY 20
typedef struct {uint8_t index;bool cw,pressed;} keyevent_t;
#define MAKE_ENCODER_CW_EVENT(i,p) ((keyevent_t){i,true,p})
#define MAKE_ENCODER_CCW_EVENT(i,p) ((keyevent_t){i,false,p})
void action_exec(keyevent_t);
void wait_ms(uint16_t);
uint32_t timer_read32(void);
uint32_t timer_elapsed32(uint32_t);
''',
'encoder.h':'#define NUM_ENCODERS 1\n',
'oled_driver.h':'''#pragma once
#include <stdint.h>
#define OLED_DISPLAY_WIDTH 128
#define OLED_DISPLAY_HEIGHT 32
void oled_write_raw_byte(char,uint16_t);
void oled_on(void);
''',
'eeprom_nrf.h':'#include "quantum.h"\n',
'eeprom.h':'''#include <stddef.h>
void eeprom_read_block(void *,const void *,size_t);
void eeprom_update_block(const void *,void *,size_t);
''',
'ble_service.h':'void reset_power_save_counter(void);\n',
'adc.h':'unsigned get_battery_level(void);\n',
'outputselect.h':'#define OUTPUT_USB 1\n#define OUTPUT_BLUETOOTH 2\nunsigned where_to_send(void);\n',
}
            for n,s in files.items(): (w/n).write_text(s)
            (w/'main.c').write_text(r'''
#include "nrf5_ui.h"
#include "nrf5_via_ui.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
static uint8_t ee[3840],screen[512],packet[32],upload[1792];
static unsigned writes,power,taps,activity;static uint16_t last;static uint32_t now;
uint32_t layer_state=1,default_layer_state=1;
uint32_t timer_read32(void){return now;}
uint32_t timer_elapsed32(uint32_t t){return now-t;}
void eeprom_read_block(void *d,const void *s,size_t n){assert((uintptr_t)s+n<=sizeof(ee));memcpy(d,ee+(uintptr_t)s,n);}
void eeprom_update_block(const void *s,void *d,size_t n){assert((uintptr_t)d==2048&&n==1792);memcpy(ee+(uintptr_t)d,s,n);writes++;}
void nrf5_oled_set_power_mode(uint8_t p){power=p;}
void oled_write_raw_byte(char p,uint16_t i){assert(i<512);screen[i]=p;}
void oled_on(void){}
unsigned get_battery_level(void){return 90;}
unsigned where_to_send(void){return 1;}
uint8_t get_highest_layer(uint32_t s){uint8_t l=0;while(s>>1){l++;s>>=1;}return l;}
bool encoder_update_user(uint8_t i,bool c){return true;}
void tap_code16(uint16_t k){taps++;last=k;}
void reset_power_save_counter(void){activity++;}
void wait_ms(uint16_t t){}
void action_exec(keyevent_t e){if(e.pressed)tap_code16(nrf5_ui_encoder_key(e.index,get_highest_layer(layer_state|default_layer_state),e.cw));}
void nrf5_ui_oled_render(void);
static uint32_t crc(const uint8_t *b,unsigned n){uint32_t c=~0u;while(n--){c^=*b++;for(int i=0;i<8;i++)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
static void seal(void){uint32_t c=crc(upload,1788);memcpy(upload+1788,&c,4);}
static void req(uint8_t verb,uint8_t op,uint16_t id,uint16_t offset,uint8_t n,uint8_t src){memset(packet,0,32);packet[0]=verb;packet[1]=0x71;packet[2]=op;packet[4]=id;packet[5]=id>>8;packet[6]=offset;packet[7]=offset>>8;packet[8]=n;assert(nrf5_ui_command(packet,src));}
static uint16_t begin(uint8_t src){req(7,1,0,0,0,src);assert(packet[3]==0);return packet[4]|packet[5]<<8;}
static void put(uint16_t id){for(unsigned i=0;i<1792;i+=23){unsigned n=1792-i;if(n>23)n=23;memset(packet,0,32);packet[0]=7;packet[1]=0x71;packet[2]=2;packet[4]=id;packet[5]=id>>8;packet[6]=i;packet[7]=i>>8;packet[8]=n;memcpy(packet+9,upload+i,n);assert(nrf5_ui_command(packet,0)&&packet[3]==0);}}
int main(void){
 memset(ee,0xff,sizeof(ee));req(8,0,0,0,0,0);assert(packet[4]==1&&packet[5]==128&&packet[6]==32&&packet[7]==1&&packet[8]==5);
 const uint8_t *active=nrf5_ui_config();memcpy(upload,active,1792);assert(!writes&&!power);
 // Screen title and font defaults survive invalid/uninitialized persistent data.
 nrf5_ui_oled_render();unsigned pixels=0;for(int i=0;i<512;i++)pixels+=screen[i]!=0;assert(pixels>0);
 uint16_t id=begin(0);req(7,1,0,0,0,1);assert(packet[3]==3);
 req(7,2,id,1790,23,0);assert(packet[3]==2);req(7,2,id+1,0,1,0);assert(packet[3]==3);req(7,2,id,0,1,1);assert(packet[3]==3);
 // Upload changes only the staging buffer. Invalid CRC cannot affect live display or Flash.
 upload[2]=1;upload[48]=0;upload[49]=42;upload[50]=0;upload[51]=43;upload[52]=0;put(id);req(9,4,id,0,0,0);assert(packet[3]==4&&!writes&&active[2]==0);
 seal();put(id);req(9,4,id,0,0,0);assert(!packet[3]&&writes==1&&power==1&&active[49]==42);for(int i=0;i<2048;i++)assert(ee[i]==255);
 assert(!nrf5_encoder_update(0,true)&&last==42&&taps==1);assert(!nrf5_encoder_update(0,false)&&last==43&&taps==2);
 packet[0]=0x14;packet[1]=0;packet[2]=0;packet[3]=1;assert(nrf5_ui_command(packet,0));assert(packet[4]==0&&packet[5]==42);
 // Invalid placement/action/dimensions are rejected even with correct CRC.
 id=begin(0);upload[16]=128;seal();put(id);req(9,4,id,0,0,0);assert(packet[3]==4&&writes==1);upload[16]=0;
 upload[48]=2;seal();put(id);req(9,4,id,0,0,0);assert(packet[3]==4&&writes==1);upload[48]=0;
 // Clip a scaled custom logo at the bottom-right without out-of-range writes.
 memset(upload+16,0,28);for(int i=0;i<7;i++)upload[18+i*4]=1;
 upload[1]|=2;upload[6]=16;upload[7]=16;memset(upload+736,255,32);upload[16]=127;upload[17]=31;upload[18]=2;upload[19]=1;
 seal();put(id);req(9,4,id,0,0,0);assert(!packet[3]);nrf5_ui_oled_render();assert(screen[511]==128);for(int i=0;i<511;i++)assert(!screen[i]);
 // Aborted/disconnected/expired uploads remain inert and free the other host.
 id=begin(0);nrf5_ui_disconnect(0);req(9,4,id,0,0,0);assert(packet[3]==3);
 id=begin(0);now=30001;req(9,4,id,0,0,0);assert(packet[3]==3);begin(1);nrf5_ui_disconnect(1);
 // Standard VIA edits map to the same persistent store and invalidate open drafts.
 id=begin(0);memset(packet,0,32);packet[0]=0x15;packet[3]=1;packet[5]=88;assert(nrf5_ui_command(packet,0));assert(writes==3);assert(!nrf5_encoder_update(0,true)&&last==88);
 req(9,4,id,0,0,0);assert(packet[3]==3);
 return 0;
}
''')
            cmd=['cc','-std=c11','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-DNRF5_VIA_UI_ENABLE','-DOLED_ENABLE','-I',str(w),'-I',str(ROOT/'platforms/nrf5/features')]
            cmd += [str(w/'main.c')]+[str(ROOT/'platforms/nrf5/features'/p) for p in ['via_ui.c','via_ui_oled.c','encoder.c']]+['-o',str(w/'test')]
            subprocess.run(cmd,check=True,capture_output=True)
            subprocess.run([str(w/'test')],check=True,capture_output=True)
    def test_fault_diagnostic_packet_and_sdk_fault_guard(self):
        if not shutil.which('cc'): self.skipTest('cc required')
        with tempfile.TemporaryDirectory() as d:
            w=Path(d)
            (w/'quantum.h').write_text('#pragma once\n#include <stdint.h>\nuint32_t timer_read32(void);\n')
            (w/'nrf.h').write_text(r'''#pragma once
#include <stdint.h>
#define __disable_irq() ((void)0)
#define __DMB() ((void)0)
void NVIC_SystemReset(void);
typedef struct {uint32_t RESETREAS;} power_t;
extern power_t mock_power;
#define NRF_POWER (&mock_power)
''')
            (w/'app_error.h').write_text(r'''#pragma once
#include <stdint.h>
#define NRF_FAULT_ID_SDK_ERROR 0x4001
#define NRF_FAULT_ID_SDK_ASSERT 0x4002
typedef struct {uint32_t line_num;const uint8_t *file;uint32_t err_code;} error_info_t;
''')
            (w/'app_util.h').write_text('#include <stdint.h>\nextern uint32_t mock_boot,mock_uicr;\n#define MBR_BOOTLOADER_ADDR ((uintptr_t)&mock_boot)\n#define MBR_UICR_BOOTLOADER_ADDR (&mock_uicr)\n')
            (w/'nrf_fstorage.h').write_text('#include <stdint.h>\ntypedef struct {uint32_t start_addr;} nrf_fstorage_t;\nextern nrf_fstorage_t mock_fs;\n#define NRF_SECTION_DEF(a,b)\n#define NRF_FSTORAGE_INSTANCE_CNT 1\n#define NRF_FSTORAGE_INSTANCE_GET(i) (&mock_fs)\n')
            (w/'test.c').write_text(r'''
#include "nrf.h"
#include "nrf_fstorage.h"
#include <assert.h>
#include <string.h>
power_t mock_power={4};nrf_fstorage_t mock_fs={0xef000};
uint32_t mock_boot=0xf4000,mock_uicr=0xe0000;static int resets;
uint32_t timer_read32(void){return 123456;}
void NVIC_SystemReset(void){resets++;}
void nrf5_ui_diagnostics(uint8_t *);
void app_error_fault_handler(uint32_t,uint32_t,uint32_t);
static uint32_t be(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
int main(void){uint8_t packet[32]={0x82};nrf5_ui_diagnostics(packet);
assert(packet[0]==0x82&&packet[1]==1&&be(packet+2)==123456&&be(packet+6)==0);
assert(be(packet+22)==0xef000&&be(packet+26)==0xf4000&&packet[31]==4);
app_error_fault_handler(0x1001,0x12345,0xabcdef);nrf5_ui_diagnostics(packet);
assert(resets==1&&be(packet+6)==0x1001&&be(packet+10)==0x12345&&be(packet+14)==0xabcdef&&be(packet+18)==0);
// Bad SDK error-info pointers must not cause a nested fault.
app_error_fault_handler(0x4001,0x45678,1);nrf5_ui_diagnostics(packet);assert(resets==2&&be(packet+18)==0);
mock_boot=0xffffffff;nrf5_ui_diagnostics(packet);assert(be(packet+26)==0xe0000);
return 0;}
''')
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(w),str(ROOT/'platforms/nrf5/features/via_ui_diagnostics.c'),str(w/'test.c'),'-o',str(w/'test')],check=True,capture_output=True)
            subprocess.run([str(w/'test')],check=True,capture_output=True)
if __name__=='__main__':unittest.main()
