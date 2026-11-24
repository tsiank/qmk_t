# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the real pad callbacks and OLED DMA path with no attached hardware."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]

class PadTests(unittest.TestCase):
    def compile(self, headers, source, flags=()):
        if not shutil.which('cc'):
            self.skipTest('Native compiler is required')
        with tempfile.TemporaryDirectory() as d:
            work=Path(d)
            for name,text in headers.items():
                (work/name).write_text(text)
            (work/'test.c').write_text(source)
            command=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g',
                     '-fsanitize=address,undefined','-fno-pie','-no-pie','-I',d,
                     '-I',str(ROOT/'keyboards/ciank_pad'),'-I',str(ROOT/'platforms/nrf5/features'),
                     '-I',str(ROOT/'drivers'),*flags,str(work/'test.c'),'-o',str(work/'test')]
            for cmd in (command,[str(work/'test')]):
                result=subprocess.run(cmd,capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'detect_stack_use_after_return=1'})
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_oled_flash_commands_ram_lifetime_and_bounded_timeouts(self):
        for shared_oled in (False, True):
            self.compile({
            'nrfx_twim.h':r'''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int nrfx_err_t;
typedef struct { int instance; } nrfx_twim_t;
typedef struct { int scl,sda,frequency,interrupt_priority,hold_bus_uninit; } nrfx_twim_config_t;
typedef struct { int type; } nrfx_twim_evt_t;
#define NRFX_TWIM_INSTANCE(n) {n}
#define NRF_TWIM_FREQ_400K 400000
#define NRFX_SUCCESS 0
int nrfx_twim_init(const nrfx_twim_t*,const nrfx_twim_config_t*,void(*)(const nrfx_twim_evt_t*,void*),void*);
void nrfx_twim_enable(const nrfx_twim_t*);
void nrfx_twim_disable(const nrfx_twim_t*);
bool nrfx_twim_is_busy(const nrfx_twim_t*);
int nrfx_twim_tx(const nrfx_twim_t*,uint8_t,const uint8_t*,size_t,bool);
int nrfx_twim_rx(const nrfx_twim_t*,uint8_t,uint8_t*,size_t);
''',
            'nrf_log.h':'#define NRF_LOG_INFO(...) do {} while(0)\n',
            'nrf_pwr_mgmt.h':'void nrf_pwr_mgmt_run(void);\n',
            'timer.h':'#include <stdint.h>\nuint32_t timer_read32(void);uint32_t timer_elapsed32(uint32_t);\n',
        },r'''
#include <assert.h>
#include <string.h>
#include "'''+str(ROOT/'drivers/nrf5/i2c_master.c')+r'''"
static uint32_t now;
static unsigned init_calls,enable_calls,disable_calls,led_writes,init_failures=1;
static bool busy,hw_enabled;
static unsigned delay=2,remaining;
static int failure,completion_type;
static const uint8_t *last_dma;
static void (*callback)(const nrfx_twim_evt_t*,void*);
uint32_t timer_read32(void){return now;}
uint32_t timer_elapsed32(uint32_t at){return now-at;}
int nrfx_twim_init(const nrfx_twim_t*a,const nrfx_twim_config_t*b,void(*cb)(const nrfx_twim_evt_t*,void*),void*d){callback=cb;init_calls++;if(init_failures){init_failures--;return 8;}return 0;}
void nrfx_twim_enable(const nrfx_twim_t*a){assert(!hw_enabled&&!busy);hw_enabled=true;enable_calls++;}
void nrfx_twim_disable(const nrfx_twim_t*a){assert(hw_enabled&&!busy);hw_enabled=false;disable_calls++;}
bool nrfx_twim_is_busy(const nrfx_twim_t*a){return busy;}
static void finish(void){nrfx_twim_evt_t event={completion_type};busy=false;callback(&event,NULL);}
void nrf_pwr_mgmt_run(void){now++;if(busy&&remaining&&!--remaining)finish();}
int nrfx_twim_tx(const nrfx_twim_t*a,uint8_t address,const uint8_t*data,size_t length,bool stop){
    assert(hw_enabled&&(address==0x3c||address==0x30)&&data==transmit_buffer&&length<=255);
    if(address==0x30){led_writes++;if(length==2&&data[0]==0xfd)assert(data[1]==4);}
    if(failure)return 9;
    last_dma=data;busy=true;remaining=delay;return 0;
}
int nrfx_twim_rx(const nrfx_twim_t*a,uint8_t address,uint8_t*data,size_t length){
    assert(hw_enabled&&data==receive_buffer);if(failure)return 9;memset(data,0x5a,length);busy=true;remaining=delay;return 0;
}
int main(void){
    static const uint8_t flash[]={0,0xae,0xa4};
    i2c_init();assert(init_calls==1&&!enable_calls&&nrf5_i2c_error_count()==1);
    i2c_init();unsigned initial_led_writes=led_writes;i2c_init();assert(init_calls==2&&enable_calls==disable_calls&&initial_led_writes==led_writes&&!hw_enabled);assert(i2c_transmit(0x78,flash,3,20)==0);assert(last_dma!=flash&&!memcmp(last_dma,flash,3)&&!hw_enabled);
    delay=0;uint32_t before=now;assert(i2c_transmit(0x78,flash,3,5)==I2C_STATUS_TIMEOUT);assert(now-before==5);
    assert(hw_enabled&&busy);unsigned enables_before=enable_calls;
    uint8_t replacement[]={1,2,3};assert(i2c_transmit(0x78,replacement,3,5)==I2C_STATUS_ERROR);assert(!memcmp(last_dma,flash,3));
    assert(hw_enabled&&enable_calls==enables_before); // timed-out DMA is still owned
    finish();delay=2;assert(i2c_write_register(0x78,0x40,replacement,3,20)==0);assert(last_dma[0]==0x40&&last_dma[3]==3);
    uint8_t output[4]={1,2,3,4};assert(i2c_receive(0x78,output,4,20)==0);assert(output[0]==0x5a);
    completion_type=1;assert(i2c_transmit(0x78,flash,3,20)==I2C_STATUS_ERROR);assert(!hw_enabled);completion_type=0;
    delay=0;memset(output,0,4);assert(i2c_receive(0x78,output,4,5)==I2C_STATUS_TIMEOUT);assert(output[0]==0);finish();
    failure=1;assert(i2c_transmit(0x78,flash,1,20)==I2C_STATUS_ERROR);
    assert(!hw_enabled);assert(i2c_receive(0x78,output,4,20)==I2C_STATUS_ERROR);assert(!hw_enabled);
    assert(i2c_transmit(0x78,NULL,1,20)==I2C_STATUS_ERROR);assert(i2c_transmit(0x78,flash,256,20)==I2C_STATUS_ERROR);
    assert(nrf5_i2c_error_count()>=4);
    failure=0;delay=2;i2c_stop();
#ifdef OLED_ENABLE
    assert(!hw_enabled&&twim_enable);assert(i2c_transmit(0x78,flash,3,20)==0);
#else
    assert(!hw_enabled&&!twim_enable);assert(i2c_transmit(0x78,flash,3,20)==I2C_STATUS_ERROR);
#endif
    assert(enable_calls==disable_calls); // success, errors and late IRQs all release clocks
    i2c_start();assert(init_calls==2);return 0;
}
''',flags=('-DRGB_MATRIX_IS31FL3741','-DDRIVER_ADDR_1=0x30',*(('-DOLED_ENABLE',) if shared_oled else ())))

    def test_encoder_layers_and_oled_status_use_current_callbacks(self):
        board=(ROOT/'keyboards/ciank_pad/ciank_pad.c').read_text()
        callbacks='\n'.join('#include \"'+str(ROOT/'platforms/nrf5/features'/name)+'\"' for name in ('encoder.c','oled_status.c'))+'\n'+board[board.index('#ifdef NRF5_ENCODER_ENABLE'):]
        self.compile({**{name:'' for name in ('ble_service.h','oled_driver.h','adc.h','outputselect.h','rgblight.h')},'quantum.h':r'''#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SAFE_RANGE 0x7e40
typedef struct {int unused;} keyrecord_t;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define KC_NO 0
#define OLED_DISPLAY_WIDTH 128
#define OLED_FONT_WIDTH 6
#define NRF5_OLED_TITLE "ciank_pad"
#define NRF5_OLED_LAYER_NAMES {"BASE","NAV","RGB","SYS","BLE"}
#define NRF5_ENCODER_BINDINGS {{0,0,NRF5_ENCODER_KEYS,KC_VOLD,KC_VOLU},{0,1,NRF5_ENCODER_KEYS,KC_PGUP,KC_PGDN},{0,2,NRF5_ENCODER_UNDERGLOW,KC_NO,KC_NO},{1,0,NRF5_ENCODER_KEYS,0x1234,KC_NO}}
#define KC_LALT 2
#define KC_LCTL 1
#define MOD_BIT(k) (1u<<(k))
enum {KC_VOLD=1,KC_VOLU,KC_PGUP,KC_PGDN};
extern uint32_t layer_state,default_layer_state;
uint8_t get_highest_layer(uint32_t);
uint8_t get_mods(void);
bool encoder_update_user(uint8_t,bool);
void tap_code16(uint16_t);
void oled_on(void);
void reset_power_save_counter(void);
void rgblight_decrease_val(void);void rgblight_increase_val(void);
void rgblight_decrease_sat(void);void rgblight_increase_sat(void);
void rgblight_decrease_hue(void);void rgblight_increase_hue(void);
bool oled_task_user(void);
void oled_set_cursor(uint8_t,uint8_t);void oled_write_ln(const char*,bool);
uint8_t where_to_send(void);
#define OUTPUT_USB 2
#define OUTPUT_BLUETOOTH 3
uint16_t get_vcc(void);uint8_t get_battery_level(void);
bool rgblight_is_enabled(void);uint8_t rgblight_get_mode(void);uint8_t rgblight_get_val(void);
typedef struct {bool num_lock,caps_lock;} led_t;
led_t host_keyboard_led_state(void);
bool nrf5_midi_typing_enabled(void);
'''},r'''
#include "ciank_pad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint32_t layer_state,default_layer_state=1;
static unsigned taps,last_key,mods,hue,val,sat,lines;
static bool override;
static char screen[4][24];
uint8_t get_highest_layer(uint32_t state){uint8_t layer=0;while(state>>1){state>>=1;layer++;}return layer;}
uint8_t get_mods(void){return mods;}
bool encoder_update_user(uint8_t a,bool b){return !override;}
void tap_code16(uint16_t key){taps++;last_key=key;}
void oled_on(void){}void reset_power_save_counter(void){}
void rgblight_decrease_val(void){val++;}void rgblight_increase_val(void){val++;}
void rgblight_decrease_sat(void){sat++;}void rgblight_increase_sat(void){sat++;}
void rgblight_decrease_hue(void){hue++;}void rgblight_increase_hue(void){hue++;}
bool oled_task_user(void){return true;}
void oled_set_cursor(uint8_t x,uint8_t y){lines=0;}
void oled_write_ln(const char*text,bool inverse){assert(lines<4&&strlen(text)<=20);strcpy(screen[lines++],text);}
uint8_t where_to_send(void){return OUTPUT_USB;}
uint16_t get_vcc(void){return 3800;}uint8_t get_battery_level(void){return 78;}
bool rgblight_is_enabled(void){return true;}uint8_t rgblight_get_mode(void){return 1;}uint8_t rgblight_get_val(void){return 128;}
led_t host_keyboard_led_state(void){return (led_t){true,false};}
bool nrf5_midi_typing_enabled(void){return false;}
'''+callbacks+r'''
int main(void){
    assert(!encoder_update_kb(0,true)&&last_key==KC_VOLD&&taps==1);
    assert(!encoder_update_kb(0,false)&&last_key==KC_VOLU&&taps==2);
    layer_state=1u<<_DIR;encoder_update_kb(0,true);assert(last_key==KC_PGUP);
    layer_state=1u<<_RGB_SETTINGS;encoder_update_kb(0,true);assert(hue==1);
    mods=MOD_BIT(KC_LALT);encoder_update_kb(0,true);assert(val==1);
    mods=MOD_BIT(KC_LCTL);encoder_update_kb(0,false);assert(sat==1);
    override=true;unsigned before=taps;encoder_update_kb(0,false);assert(taps==before);override=false;
    layer_state=1u<<_SYS;before=taps;assert(!encoder_update_kb(0,true)&&taps==before);
    layer_state=0;assert(!encoder_update_kb(1,true)&&last_key==0x1234);
    before=taps;assert(!encoder_update_kb(1,false)&&taps==before);
    assert(encoder_update_kb(3,true)&&taps==before);
    assert(!oled_task_kb()&&lines==4);
    assert(strstr(screen[0],"BASE")&&strstr(screen[0],"USB")&&strstr(screen[1],"3800mV")&&strstr(screen[2],"RGB ON")&&strstr(screen[3],"NUM:1 CAPS:0"));
    layer_state=1u<<8;assert(!oled_task_kb()&&strstr(screen[0],"L8"));
    return 0;
}
''', ['-DOLED_ENABLE','-DENCODER_ENABLE','-DNRF5_ENCODER_ENABLE','-DNRF5_OLED_STATUS_ENABLE','-DNRF5_CONFIG_ENABLE','-DRGBLIGHT_ENABLE'])

    def test_matrix_select_delay_is_not_a_noop(self):
        self.compile({'wait_api.h':'#include <stdint.h>\nvoid wait_us(uint32_t);\n'},r'''
#include <assert.h>
#include "'''+str(Path.cwd()/'platforms/nrf5/_wait.h')+r'''"
static unsigned elapsed;
void wait_us(uint32_t us){elapsed+=us;}
int main(void){waitInputPinDelay();assert(elapsed==GPIO_INPUT_PIN_DELAY&&elapsed>=2);return 0;}
''')

    def test_usb_media_press_release_buffers_survive_async_dma_and_busy(self):
        code=(ROOT/'tmk_core/protocol/nrf5/usb/nrf5_usb.c').read_text()
        begin=code.index('#define EXTRA_QUEUE_LENGTH');end=code.index('\n#endif',begin)
        self.compile({},r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define NRF_SUCCESS 0
#define REPORT_ID_SYSTEM 1
#define REPORT_ID_CONSUMER 2
static struct{unsigned ENABLE;}hw={1};
#define NRF_USBD (&hw)
static bool m_report_pending;
static int m_app_hid_extra;
static const uint8_t *dma;
static unsigned calls, fail;
static uint16_t system_state,consumer_state;
unsigned app_usbd_hid_generic_in_report_set(int *device,const uint8_t *data,unsigned length){
    assert(device==&m_app_hid_extra&&length==3);calls++;if(fail)return 17;dma=data;return 0;
}
'''+code[begin:end]+r'''
static void done(void){
    assert(extra_active&&dma);
    uint16_t value=dma[1]|((uint16_t)dma[2]<<8);
    if(dma[0]==REPORT_ID_SYSTEM)system_state=value;else consumer_state=value;
    extra_done();nrf5_usb_extra_task();
}
int main(void){
    hid_extra_process_state(REPORT_ID_CONSUMER,0xe9);const uint8_t *pressed=dma;
    hid_extra_process_state(REPORT_ID_CONSUMER,0);assert(pressed[1]==0xe9&&extra_count==2&&calls==1);
    done();assert(consumer_state==0xe9&&extra_active&&dma[1]==0);done();assert(!extra_count&&!consumer_state);
    fail=1;hid_extra_process_state(REPORT_ID_CONSUMER,0xe9);assert(!extra_active&&extra_count==1);
    fail=0;nrf5_usb_extra_task();assert(extra_active);done();
    hid_extra_process_state(REPORT_ID_CONSUMER,0xe9);pressed=dma;
    for(unsigned i=0;i<40;i++)hid_extra_process_state(REPORT_ID_SYSTEM,i+1);
    assert(pressed[1]==0xe9);hid_extra_process_state(REPORT_ID_SYSTEM,0);
    while(extra_count){done();}
    assert(system_state==0&&consumer_state==0);
    extra_reset();assert(!extra_active&&!extra_count);
    hw.ENABLE=0;hid_extra_process_state(REPORT_ID_CONSUMER,0xe9);assert(extra_count==0);
    return 0;
}
''')

    def test_layout_matches_twenty_one_keys_and_old_macro_cells(self):
        import json
        definition=json.loads((ROOT/'keyboards/ciank_pad/keyboard.json').read_text())
        profile=json.loads((ROOT/'util/ciank67_music/WinUI/Qmknrf5.App/Layouts/ciank_pad.json').read_text())
        keys=definition['layouts']['LAYOUT']['layout']
        self.assertEqual(keys,profile['layout'])
        self.assertEqual(len(keys),21)
        self.assertEqual({tuple(k['matrix']) for k in keys},{(0,4)}|{(r,c) for r in range(1,5) for c in range(5)})
        self.assertEqual(profile['matrix'],{'rows':5,'cols':5})
        self.assertEqual(profile['custom_keycodes']['VOLUP'],0x7e40+14)
        self.assertEqual(definition['rgblight']['led_count'],20)


    def test_usb_blocks_automatic_sleep_and_battery_wake_pins(self):
        service=(ROOT/'tmk_core/protocol/nrf5/ble/ble_service.c').read_text()
        def function(signature):
            start=service.index(signature+' {')
            pos=service.index('{',start)+1
            depth=1
            while depth:
                if service[pos]=='{': depth+=1
                elif service[pos]=='}': depth-=1
                pos+=1
            return service[start:pos]
        functions='\n'.join(function(name) for name in (
            'static void request_automatic_sleep(void)',
            'static void battery_level_meas_timeout_handler(void *p_context)',
            'void reset_power_save_counter(void)',
            'void nrf5_power_task(void)',
            'static void sleep_mode_enter(void)'))
        idle=service.split('case BLE_ADV_EVT_IDLE:')[1].split('break;')[0]
        for direction in (1,2):
            with self.subTest(direction=direction):
                self.compile({'nrf.h':r'''#pragma once
#include <stdint.h>
static struct {uint32_t USBREGSTATUS;} power_reg;
static struct {uint32_t ENABLE;} usb_reg;
#define NRF_POWER (&power_reg)
#define NRF_USBD (&usb_reg)
#define POWER_USBREGSTATUS_VBUSDETECT_Msk 1
'''},r'''
#include <assert.h>
#include <setjmp.h>
#include "'''+str(ROOT/'platforms/nrf5/nrf5_power.h')+r'''"
#define POWER_SAVE_TIMEOUT 2
#define RGBLIGHT_ENABLE
#define MATRIX_ROWS 2
#define MATRIX_COLS 3
#define MATRIX_ROW_PINS {1,2}
#define MATRIX_COL_PINS {3,4,5}
#define ROW2COL 1
#define NRF_GPIO_PIN_PULLUP 1
#define NRF_GPIO_PIN_SENSE_LOW 0
#define UNUSED_PARAMETER(x) (void)(x)
#define NRF_LOG_INFO(...) (void)0
#define APP_ERROR_CHECK(x) (void)(x)
typedef int ret_code_t;
static volatile uint16_t power_save_counter;
static volatile bool automatic_sleep_pending;
static bool pending,ble_disable;
static int irq,lights,sleeps,adc,outputs,senses,clears;
static jmp_buf off;
static void sleep_mode_enter(void);
bool nrf5_eeprom_pending(void){return pending;}
void adc_start(void){adc++;}
void rgblight_disable_noeeprom(void){assert(!irq);lights++;}
void nrf_gpio_cfg_output(int pin){assert(!irq);outputs|=1<<pin;}
void nrf_gpio_pin_clear(int pin){clears|=1<<pin;}
void nrf_gpio_cfg_sense_input(int pin,int pull,int sense){assert(!irq&&pull==1&&sense==0);senses|=1<<pin;}
int sd_power_system_off(void){assert(!irq);sleeps++;longjmp(off,1);return 0;}
'''+functions+r'''
static void adv_idle(void){'''+idle+r'''}
int main(void){
    // Queued USB enumeration has not enabled USBD yet.
    power_reg.USBREGSTATUS=1;irq=1;
    adv_idle();battery_level_meas_timeout_handler(0);irq=0;
    nrf5_power_task();sleep_mode_enter();
    assert(!automatic_sleep_pending&&!sleeps&&!lights&&!adc);
    // Active peripheral also protects suspend / transient power events.
    power_reg.USBREGSTATUS=0;usb_reg.ENABLE=1;
    adv_idle();nrf5_power_task();assert(!sleeps&&!automatic_sleep_pending);
    usb_reg.ENABLE=0;
    // Timer IRQ must only queue shutdown, never drive LEDs/GPIO or System OFF.
    irq=1;for(int i=0;i<4;i++)battery_level_meas_timeout_handler(0);irq=0;
    assert(automatic_sleep_pending&&!sleeps&&!lights&&adc==4);
    reset_power_save_counter();nrf5_power_task();assert(!sleeps);
    adv_idle();power_reg.USBREGSTATUS=1;nrf5_power_task();
    assert(!automatic_sleep_pending&&!lights&&!sleeps);
    power_reg.USBREGSTATUS=0;irq=1;adv_idle();irq=0;
    pending=true;nrf5_power_task();assert(automatic_sleep_pending&&!lights);
    pending=false;
    if(!setjmp(off)){nrf5_power_task();assert(0);}
    assert(sleeps==1&&lights==1&&!automatic_sleep_pending);
#if DIODE_DIRECTION == ROW2COL
    assert(outputs==((1<<3)|(1<<4)|(1<<5))&&senses==((1<<1)|(1<<2)));
#else
    assert(senses==((1<<3)|(1<<4)|(1<<5))&&outputs==((1<<1)|(1<<2)));
#endif
    assert(clears==outputs);
    return 0;
}
''',flags=('-DDIODE_DIRECTION='+str(direction),))

    def test_switching_keymap_resets_via_once_and_preserves_later_edits(self):
        code=(ROOT/'keyboards/ciank_pad/ciank_pad.c').read_text()
        hook=code[code.index('void via_init_kb(void)'):code.index('\n#endif')]
        via=(ROOT/'quantum/via.c').read_text()
        init=via[via.index('void via_init(void)'):via.index('\nvoid eeconfig_init_via(void)')]
        self.compile({},r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
static uint8_t stored_id,firmware_id=1;
static bool valid=true;
static unsigned resets,edits=42;
#define CIANK_PAD_KEYMAP_ID firmware_id
uint32_t via_read_custom_config(void *buf,uint32_t offset,uint32_t length){assert(offset==0&&length==1);*(uint8_t*)buf=stored_id;return 1;}
uint32_t via_update_custom_config(const void *buf,uint32_t offset,uint32_t length){assert(offset==0&&length==1);stored_id=*(const uint8_t*)buf;return 1;}
void via_eeprom_set_valid(bool v){valid=v;}
bool via_eeprom_is_valid(void){return valid;}
uint32_t via_get_layout_options(void){return 0;}
void via_set_layout_options_kb(uint32_t v){}
void eeconfig_init_via(void){resets++;edits=0;valid=true;}
'''+hook+init+r'''
int main(void){
    // Upgrade from the previous untagged storage must initialize shifted data.
    via_init();assert(resets==1&&stored_id==1&&!edits);
    edits=42;via_init();assert(resets==1&&edits==42);
    // Same-day default -> test must not reuse default's permanent layer keys.
    firmware_id=2;via_init();assert(resets==2&&stored_id==2&&!edits);
    edits=64;via_init();assert(resets==2&&edits==64);
    firmware_id=1;via_init();assert(resets==3&&stored_id==1&&!edits);
    // VIA's own date/magic check still triggers its normal reset.
    valid=false;via_init();assert(resets==4);
    return 0;
}
''')

    def test_shared_encoder_default_needs_no_keyboard_or_lighting_features(self):
        self.compile({'ble_service.h':'void reset_power_save_counter(void);\n','quantum.h':r'''#pragma once
#include <stdint.h>
#include <stdbool.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define KC_NO 0
#define KC_VOLU 0x80
#define KC_VOLD 0x81
extern uint32_t layer_state,default_layer_state;
uint8_t get_highest_layer(uint32_t);
bool encoder_update_user(uint8_t,bool);
void tap_code16(uint16_t);
'''},r'''
#include <assert.h>
#include "'''+str(ROOT/'platforms/nrf5/features/encoder.c')+r'''"
uint32_t layer_state,default_layer_state=1;
static bool override;
static unsigned taps,activity;
static uint16_t last;
uint8_t get_highest_layer(uint32_t state){uint8_t layer=0;while(state>>1){state>>=1;layer++;}return layer;}
bool encoder_update_user(uint8_t index,bool clockwise){return !override;}
void tap_code16(uint16_t key){last=key;taps++;}
void reset_power_save_counter(void){activity++;}
int main(void){
    assert(!nrf5_encoder_update(0,true)&&last==KC_VOLU&&taps==1&&activity==1);
    assert(!nrf5_encoder_update(0,false)&&last==KC_VOLD&&taps==2);
    override=true;assert(!nrf5_encoder_update(0,true)&&taps==2&&activity==2);
    override=false;assert(nrf5_encoder_update(1,true)&&taps==2&&activity==2);
    layer_state=2;assert(!nrf5_encoder_update(0,true)&&taps==2&&activity==3);
    return 0;
}
''')

    def test_chinese_bitmap_title_orientation_and_status_do_not_overlap(self):
        self.compile({**{name:'' for name in ('oled_driver.h','adc.h','outputselect.h')},'quantum.h':r'''#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define OLED_DISPLAY_WIDTH 128
#define OLED_DISPLAY_HEIGHT 32
#define OLED_FONT_WIDTH 6
#define OUTPUT_USB 2
#define OUTPUT_BLUETOOTH 3
#define NRF5_OLED_TITLE_BITMAP_HEADER "oled_title.h"
#define NRF5_OLED_LAYER_NAMES {"BASE0123456789","NAV","RGB","SYS","BLE"}
extern uint32_t layer_state,default_layer_state;
uint8_t get_highest_layer(uint32_t);
uint8_t where_to_send(void);
bool oled_task_user(void);
void oled_set_cursor(uint8_t,uint8_t);
void oled_write_ln(const char*,bool);
void oled_write_raw_byte(char,uint16_t);
typedef struct {bool num_lock,caps_lock;} led_t;
led_t host_keyboard_led_state(void);
'''},r'''
#include <assert.h>
#include <string.h>
#include "'''+str(ROOT/'platforms/nrf5/features/oled_status.c')+r'''"
uint32_t layer_state,default_layer_state=1;
static uint8_t frame[512],column,row,output=OUTPUT_USB;
static unsigned text_calls,raw_calls;
static bool replace;
uint8_t get_highest_layer(uint32_t state){uint8_t layer=0;while(state>>1){state>>=1;layer++;}return layer;}
uint8_t where_to_send(void){return output;}
bool oled_task_user(void){return !replace;}
led_t host_keyboard_led_state(void){return (led_t){true,false};}
void oled_set_cursor(uint8_t x,uint8_t y){column=x;row=y;assert(x<21&&y<4);}
void oled_write_ln(const char *text,bool invert){
    assert(strlen(text)<=21u-column&&!invert);
    if(row<2)assert(column==11&&strlen(text)<21u-column);
    else assert(column==0);
    // Simulate clearing/writing through the end of the current page.
    memset(frame+row*128+column*6,0xa5,128-column*6);
    row++;column=0;text_calls++;
}
void oled_write_raw_byte(char value,uint16_t index){assert(index<512);frame[index]=(uint8_t)value;raw_calls++;}
int main(void){
    assert(!nrf5_oled_task()&&text_calls==4&&raw_calls==128);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<64;x++){
        unsigned actual=(frame[(y/8)*128+x]>>(y%8))&1;
        unsigned expected=(nrf5_oled_title_bitmap[y*8+x/8]>>(7-x%8))&1;
        assert(actual==expected);
    }
    // First two source rows of 紫: keep the user's PCtoLCD row/MSB layout.
    assert(nrf5_oled_title_bitmap[0]==0x08&&nrf5_oled_title_bitmap[1]==0x80);
    assert(nrf5_oled_title_bitmap[8]==0x28&&nrf5_oled_title_bitmap[9]==0x88);
    layer_state=1u<<8;output=OUTPUT_BLUETOOTH;
    assert(!nrf5_oled_task()&&text_calls==8&&raw_calls==256);
    replace=true;assert(!nrf5_oled_task()&&text_calls==8&&raw_calls==256);
    return 0;
}
''')

    def test_traditional_clock_chu_zheng_quarters_and_rtc_wrap(self):
        self.compile({'app_timer.h':'#include <stdint.h>\nuint32_t app_timer_cnt_get(void);uint32_t app_timer_cnt_diff_compute(uint32_t,uint32_t);\n'},r'''
#include <assert.h>
#include "'''+str(ROOT/'platforms/nrf5/features/clock.c')+r'''"
static uint32_t ticks;
uint32_t app_timer_cnt_get(void){return ticks;}
uint32_t app_timer_cnt_diff_compute(uint32_t a,uint32_t b){return (a-b)&0xffffff;}
static void advance(uint32_t delta){ticks=(ticks+delta)&0xffffff;nrf5_clock_task();}
static void expect(uint8_t h,uint8_t m,uint8_t s){uint8_t a,b,c;assert(nrf5_clock_read(&a,&b,&c)&&a==h&&b==m&&c==s);}
int main(void){
    uint8_t h,m,s;assert(!nrf5_clock_read(&h,&m,&s));
    nrf5_clock_set(24,0,0,0);assert(!nrf5_clock_read(&h,&m,&s));
    const uint8_t branches[24]={0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,0};
    for(unsigned hour=0;hour<24;hour++)for(unsigned minute=0;minute<60;minute++){
        assert(nrf5_clock_branch(hour)==branches[hour]);
        assert(nrf5_clock_is_zheng(hour)==((hour&1)==0));
        assert(nrf5_clock_quarter(minute)==(minute<15?0:minute<30?1:minute<45?2:3));
    }
    assert(nrf5_clock_branch(11)==6&&!nrf5_clock_is_zheng(11)&&nrf5_clock_quarter(45)==3); // 午初三刻
    assert(nrf5_clock_branch(12)==6&&nrf5_clock_is_zheng(12)&&nrf5_clock_quarter(0)==0); // 午正初刻
    assert(nrf5_clock_branch(23)==0&&!nrf5_clock_is_zheng(23)); // 子初初刻
    assert(nrf5_clock_branch(0)==0&&nrf5_clock_is_zheng(0)); // 子正初刻
    ticks=0xfffff0;nrf5_clock_set(11,59,59,0);advance(32768);expect(12,0,0);
    nrf5_clock_set(23,59,59,999);advance(33);expect(0,0,0);
    nrf5_clock_set(11,44,59,999);advance(33);expect(11,45,0);
    nrf5_clock_set(7,8,9,0);for(int i=0;i<32768;i++)advance(1);expect(7,8,10);
    // One initial host calibration, then a full day without further host packets.
    // Exact 32768 Hz conversion includes many RTC rollovers.
    nrf5_clock_set(1,2,3,0);for(int i=0;i<288;i++)advance(300u*32768u);expect(1,2,3);
    nrf5_clock_set(0,60,0,0);expect(1,2,3);
    nrf5_clock_set(0,0,60,0);expect(1,2,3);
    nrf5_clock_set(0,0,0,1000);expect(1,2,3);
    return 0;
}
''')

    def test_clock_sync_command_is_validated_and_runs_only_in_main_loop(self):
        self.compile({'quantum.h':'#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef int keyrecord_t;\n',
                      'app_util_platform.h':'#define CRITICAL_REGION_ENTER() do {} while(0)\n#define CRITICAL_REGION_EXIT() do {} while(0)\n',
                      'app_timer.h':'#include <stdint.h>\nuint32_t app_timer_cnt_get(void);uint32_t app_timer_cnt_diff_compute(uint32_t,uint32_t);\n',
                      'oled_driver.h':'#include <stdbool.h>\nbool is_oled_on(void);\n',
                      'timer.h':'#include <stdint.h>\nuint32_t timer_read32(void);uint32_t timer_elapsed32(uint32_t);\n'},r'''
#include <assert.h>
#include <string.h>
#define KEYBOARD_CONTROL_ENABLE
#define NRF5_CLOCK_ENABLE
#define NRF5_OLED_STATUS_ENABLE
#include "'''+str(ROOT/'platforms/nrf5/features/clock.c')+r'''"
#include "'''+str(ROOT/'platforms/nrf5/music_stream.c')+r'''"
#include "'''+str(ROOT/'platforms/nrf5/features/keyboard_control.c')+r'''"
static uint8_t screen_mode;
uint8_t nrf5_oled_get_power_mode(void){return screen_mode;}
void nrf5_oled_set_power_mode(uint8_t mode){screen_mode=mode;}
bool is_oled_on(void){return screen_mode!=2;}
uint32_t app_timer_cnt_get(void){return 0;}
uint32_t app_timer_cnt_diff_compute(uint32_t a,uint32_t b){return 0;}
uint32_t timer_read32(void){return 0;}uint32_t timer_elapsed32(uint32_t t){return 0;}
bool nrf5_midi_typing_enabled(void){return false;}
void nrf5_midi_typing_set(bool on){assert(0);}
static uint8_t ack[20];static unsigned usb_replies,ble_replies;
bool keyboard_control_usb_reply(const uint8_t *p){memcpy(ack,p,20);usb_replies++;return true;}
bool keyboard_control_ble_reply(const uint8_t *p){memcpy(ack,p,20);ble_replies++;return true;}
int main(void){
    uint8_t p[20]={0xc7,4,1},h,m,s;
    assert(music_stream_receive(p,20,MUSIC_SOURCE_USB));nrf5_keyboard_control_task();
    assert(ack[5]==3&&ack[8]==3&&!ack[9]);
    p[1]=5;p[2]=2;p[3]=11;p[4]=45;p[5]=23;p[6]=0xe7;p[7]=3;
    assert(music_stream_receive(p,20,MUSIC_SOURCE_USB));assert(!nrf5_clock_read(&h,&m,&s));
    nrf5_keyboard_control_task();assert(nrf5_clock_read(&h,&m,&s)&&h==11&&m==45&&s==23);
    assert(usb_replies==2&&ack[2]==2&&ack[9]==1);
    p[3]=23;p[4]=0;p[5]=0;p[6]=p[7]=0;
    assert(music_stream_receive(p,20,MUSIC_SOURCE_BLE));nrf5_keyboard_control_task();
    assert(ble_replies==1&&nrf5_clock_read(&h,&m,&s)&&h==23&&m==0&&s==0);
    p[1]=6;p[2]=3;p[3]=2;
    assert(music_stream_receive(p,20,MUSIC_SOURCE_USB)&&screen_mode==0);
    nrf5_keyboard_control_task();assert(screen_mode==2&&ack[10]==2&&!ack[11]);
    p[3]=1;assert(music_stream_receive(p,20,MUSIC_SOURCE_BLE));nrf5_keyboard_control_task();assert(screen_mode==1&&ack[10]==1&&ack[11]);
    p[3]=3;assert(!music_stream_receive(p,20,MUSIC_SOURCE_USB));p[3]=0;
    p[4]=1;assert(!music_stream_receive(p,20,MUSIC_SOURCE_USB));p[4]=0;
    p[1]=5;
    p[3]=24;assert(!music_stream_receive(p,20,MUSIC_SOURCE_USB));p[3]=0;
    p[4]=60;assert(!music_stream_receive(p,20,MUSIC_SOURCE_USB));p[4]=0;
    p[5]=60;assert(!music_stream_receive(p,20,MUSIC_SOURCE_USB));p[5]=0;
    p[6]=0xe8;p[7]=3;assert(!music_stream_receive(p,20,MUSIC_SOURCE_USB));p[6]=p[7]=0;
    p[19]=1;assert(!music_stream_receive(p,20,MUSIC_SOURCE_USB));
    return 0;
}
''',flags=('-I'+str(ROOT/'platforms/nrf5'),))

    def test_traditional_clock_display_uses_chu_zheng_glyphs_and_keeps_title(self):
        for prefix in (None, "test-logo.h", "oled_logo.h"):
            self.compile({'test-logo.h':'#define NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH 16\n#define NRF5_OLED_TITLE_PREFIX_BITMAP_HEIGHT 16\nstatic const uint8_t nrf5_oled_title_prefix_bitmap[32]={[0 ... 31]=0x81};\n',**{name:'' for name in ('oled_driver.h','adc.h','outputselect.h')},'quantum.h':r'''#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define OLED_DISPLAY_WIDTH 128
#define OLED_DISPLAY_HEIGHT 32
#define OLED_FONT_WIDTH 6
#define OUTPUT_USB 2
#define OUTPUT_BLUETOOTH 3
#define NRF5_OLED_TITLE_BITMAP_HEADER "oled_title.h"
#define NRF5_OLED_LAYER_NAMES {"BASE","RGB"}
#define NRF5_OLED_BATTERY_AVAILABLE 0
extern uint32_t layer_state,default_layer_state;
uint8_t get_highest_layer(uint32_t);
uint8_t where_to_send(void);
bool oled_task_user(void);
uint8_t get_battery_level(void);
void oled_set_cursor(uint8_t,uint8_t);
void oled_write_ln(const char*,bool);
void oled_write_raw_byte(char,uint16_t);
'''},r'''
#include <assert.h>
#include <string.h>
#include "'''+str(ROOT/'platforms/nrf5/features/oled_status.c')+r'''"
uint32_t layer_state,default_layer_state=1;
static uint8_t frame[512],hour,minute;
static bool valid;

uint8_t get_highest_layer(uint32_t state){uint8_t l=0;while(state>>1){state>>=1;l++;}return l;}
uint8_t where_to_send(void){return OUTPUT_USB;}
bool oled_task_user(void){return true;}
uint8_t get_battery_level(void){assert(0);return 0;}
void oled_set_cursor(uint8_t x,uint8_t y){assert(0);}
void oled_write_ln(const char *text,bool invert){assert(0);}
void oled_write_raw_byte(char data,uint16_t index){assert(index<512);frame[index]=(uint8_t)data;}
bool nrf5_clock_read(uint8_t *h,uint8_t *m,uint8_t *s){*h=hour;*m=minute;*s=0;return valid;}
uint8_t nrf5_clock_branch(uint8_t h){return ((h+1)/2)%12;}
bool nrf5_clock_is_zheng(uint8_t h){return h%2==0;}
uint8_t nrf5_clock_quarter(uint8_t m){return m/15;}
static void expected_ascii(unsigned page,const char *top,const char *bottom,unsigned scale){
    unsigned start=page==0&&NRF5_OLED_TITLE_RENDER_WIDTH>68?NRF5_OLED_TITLE_RENDER_WIDTH:68;
    for(unsigned y=0;y<16;y++)for(unsigned x=start;x<128;x++){
        const char *text=scale==2||y<8?top:bottom;
        unsigned i=(x-start)/(6*scale),gx=((x-start)%(6*scale))/scale;
        unsigned bit=0;
        if(i<strlen(text))bit=(oled_text_font[(unsigned char)text[i]-32][gx]>>(scale==2?y/2:y%8))&1;
        assert(((frame[(page+y/8)*128+x]>>(y%8))&1)==bit);
    }
}
static void expected(const uint8_t *ids,unsigned count){
    unsigned left=0;
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<68;x++){
        unsigned bit=0;
        if(x>=left&&x<left+count*16){
            unsigned i=(x-left)/16,gx=(x-left)%16;
            bit=(traditional_clock_glyphs[ids[i]][y*2+gx/8]>>(7-gx%8))&1;
        }
        assert(((frame[(y/8+2)*128+x]>>(y%8))&1)==bit);
    }
    char top[24],bottom[24];
    if(valid)snprintf(top,sizeof(top),"%02u:%02u --%%",hour,minute);else strcpy(top,"--:-- --%");
    uint8_t layer=get_highest_layer(layer_state|default_layer_state);
    snprintf(bottom,sizeof(bottom),"USB L%u",layer);
    expected_ascii(2,top,bottom,1);
    char label[5];snprintf(label,sizeof(label),"L%u",layer);
    expected_ascii(0,layer==0?"BASE":layer==1?"RGB":label,"",2);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<NRF5_OLED_TITLE_RENDER_WIDTH;x++){
        unsigned sx=x;const uint8_t *source=nrf5_oled_title_bitmap;unsigned stride=8;
#ifdef NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER
        if(x<16){source=nrf5_oled_title_prefix_bitmap;stride=2;}else sx-=16;
#endif
        assert(((frame[(y/8)*128+x]>>(y%8))&1)==((source[y*stride+sx/8]>>(7-sx%8))&1));
    }
}
int main(void){
    nrf5_oled_task();expected((uint8_t[]){18,19,20},3);
    valid=true;hour=11;minute=45;nrf5_oled_task();expected((uint8_t[]){6,12,16,17},4);
    hour=12;minute=0;nrf5_oled_task();expected((uint8_t[]){6,13,12,17},4);
    hour=23;minute=0;nrf5_oled_task();expected((uint8_t[]){0,12,12,17},4);
    hour=0;minute=0;nrf5_oled_task();expected((uint8_t[]){0,13,12,17},4);
    layer_state=2;nrf5_oled_task();expected((uint8_t[]){0,13,12,17},4);
    layer_state=1u<<8;nrf5_oled_task();expected((uint8_t[]){0,13,12,17},4);
    return 0;
}
''',flags=('-DNRF5_CLOCK_ENABLE',*(('-DNRF5_OLED_TITLE_PREFIX_BITMAP_HEADER="'+prefix+'"',) if prefix else ())))

    def test_oled_usb_default_and_manual_off_block_render_and_key_wakes(self):
        driver=(ROOT/'drivers/oled/oled_driver.c').read_text()
        on=driver[driver.index('bool oled_on(void) {'):driver.index('\nbool oled_off(void) {')]
        off=driver[driver.index('bool oled_off(void) {'):driver.index('\nbool is_oled_on(void) {')]
        self.compile({'quantum.h':'#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n',
                      'nrf5_power.h':'#include <stdbool.h>\nextern bool usb;\nstatic inline bool nrf5_usb_powered(void){return usb;}\n',
                      'keyboard.h':'#include <stdint.h>\nuint32_t last_input_activity_time(void);uint32_t last_input_activity_elapsed(void);\n',
                      'oled_driver.h':'#include <stdbool.h>\nbool oled_on(void);bool oled_off(void);\n'},r'''
#include <assert.h>
#include "'''+str(ROOT/'platforms/nrf5/features/oled_power.c')+r'''"
#define PROGMEM
#define I2C_CMD 0
#define DISPLAY_ON 0xaf
#define DISPLAY_OFF 0xae
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define print(...) (void)0
static bool oled_initialized=true,oled_active;
static uint32_t oled_timeout;
static unsigned commands;
bool usb=true;
static uint32_t activity,elapsed;
uint32_t last_input_activity_time(void){return activity;}
uint32_t last_input_activity_elapsed(void){return elapsed;}
uint32_t timer_read32(void){return 0;}
bool oled_send_cmd_P(const uint8_t *data,unsigned size){commands++;return true;}
'''+on+off+r'''
int main(void){
    elapsed=70000;assert(nrf5_oled_get_power_mode()==0&&nrf5_oled_wake_allowed());
    nrf5_oled_power_task();assert(oled_active&&commands==1);
    nrf5_oled_set_power_mode(2);assert(!oled_active&&!nrf5_oled_wake_allowed());
    unsigned before=commands;
    for(int i=0;i<100;i++){activity++;elapsed=0;oled_on();nrf5_oled_power_task();}
    assert(!oled_active&&commands==before); // keys/encoder/dirty render cannot wake manual OFF
    nrf5_oled_set_power_mode(1);assert(oled_active);usb=false;elapsed=70000;
    assert(nrf5_oled_wake_allowed()); // forced ON also applies on battery
    nrf5_oled_set_power_mode(0);assert(!oled_active);
    elapsed=5;assert(!nrf5_oled_wake_allowed()); // timer rollover is not real activity
    activity++;elapsed=0;nrf5_oled_power_task();assert(oled_active);
    elapsed=59999;assert(nrf5_oled_wake_allowed());
    elapsed=60000;nrf5_oled_power_task();assert(!oled_active);
    usb=true;nrf5_oled_power_task();assert(oled_active);
    nrf5_oled_set_power_mode(3);assert(nrf5_oled_get_power_mode()==0);
    return 0;
}
''',flags=('-DNRF5_OLED_STATUS_ENABLE','-DOLED_TIMEOUT=60000'))
