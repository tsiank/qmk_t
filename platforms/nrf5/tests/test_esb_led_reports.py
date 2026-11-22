# SPDX-License-Identifier: GPL-2.0-or-later
"""Observe host LED writes without mistaking unrelated HID writes for Caps."""
import unittest
import test_esb as helpers

class LedReportTests(unittest.TestCase):
    def test_ble_diagnostics_capture_led_payload_and_ignore_invalid_writes(self):
        text=(helpers.ROOT/'tmk_core/protocol/nrf5/ble/ble_hids_service.c').read_text()
        start=text.index('static void on_hids_evt(ble_hids_t *p_hids, ble_hids_evt_t *p_evt) {')
        end=text.index('\nstatic uint8_t keyboard_leds(void);',start)
        actual=text[start:end]
        helpers.EsbTests.run_c(self,r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
enum {BLE_HIDS_EVT_BOOT_MODE_ENTERED=1,BLE_HIDS_EVT_REPORT_MODE_ENTERED,BLE_HIDS_EVT_REP_CHAR_WRITE,BLE_HIDS_EVT_NOTIF_ENABLED};
#define BLE_HIDS_REP_TYPE_OUTPUT 2
#define BLE_UUID_BOOT_KEYBOARD_OUTPUT_REPORT_CHAR 0x2A32
typedef int ble_hids_t;
typedef struct {uint8_t evt_type;struct {struct {struct {uint16_t uuid;uint8_t rep_type;} char_id;uint16_t offset,len;const uint8_t *data;} char_write;} params;} ble_hids_evt_t;
static bool m_in_boot_mode;
volatile uint8_t nrf5_ble_led_last_report,nrf5_ble_led_report_count;
static unsigned calls;
static void on_hid_rep_char_write(ble_hids_evt_t *evt){(void)evt;calls++;}
'''+actual+r'''
int main(void){
 ble_hids_evt_t event={.evt_type=BLE_HIDS_EVT_REP_CHAR_WRITE};uint8_t payload=2;
 event.params.char_write.char_id.rep_type=2;event.params.char_write.len=1;event.params.char_write.data=&payload;
 on_hids_evt(NULL,&event);assert(nrf5_ble_led_report_count==1&&nrf5_ble_led_last_report==2);
 payload=0;on_hids_evt(NULL,&event);assert(nrf5_ble_led_report_count==2&&nrf5_ble_led_last_report==0);
 event.params.char_write.offset=1;on_hids_evt(NULL,&event);assert(nrf5_ble_led_report_count==2);
 event.params.char_write.offset=0;event.params.char_write.len=0;on_hids_evt(NULL,&event);assert(nrf5_ble_led_report_count==2);
 event.params.char_write.len=1;event.params.char_write.data=NULL;on_hids_evt(NULL,&event);assert(nrf5_ble_led_report_count==2);
 event.params.char_write.data=&payload;event.params.char_write.char_id.rep_type=3;on_hids_evt(NULL,&event);assert(nrf5_ble_led_report_count==2);
 payload=2;event.params.char_write.char_id.rep_type=0;event.params.char_write.char_id.uuid=BLE_UUID_BOOT_KEYBOARD_OUTPUT_REPORT_CHAR;
 on_hids_evt(NULL,&event);assert(nrf5_ble_led_report_count==3&&nrf5_ble_led_last_report==2&&calls==7);
}
''',flags=['-DNRF5_ESB_TX','-Wno-unused-parameter'])

if __name__=='__main__':unittest.main()
