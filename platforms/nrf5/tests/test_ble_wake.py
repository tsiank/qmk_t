# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real GAP/advertising recovery and long-idle power policy without USB."""
import unittest
import test_esb as helpers


class BleWakeTests(unittest.TestCase):
    def test_adv_expiry_disconnect_and_user_input_restart_in_main_context(self):
        service=(helpers.ROOT/'tmk_core/protocol/nrf5/ble/ble_service.c').read_text()
        def function(signature):
            start=service.index(signature+' {')
            pos=service.index('{',start)+1
            depth=1
            while depth:
                depth+=(service[pos]=='{')-(service[pos]=='}')
                pos+=1
            return service[start:pos]
        idle=service.split('case BLE_ADV_EVT_IDLE:')[1].split('break;')[0]
        disconnected=service.split('case BLE_GAP_EVT_DISCONNECTED:')[1].split('case BLE_GAP_EVT_PHY_UPDATE_REQUEST:')[0]
        functions='\n'.join(function(signature) for signature in (
            'void reset_power_save_counter(void)', 'void nrf5_power_task(void)', 'void nrf5_ble_reconnect_task(void)'))
        helpers.EsbTests.run_c(self,r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define NRF5_MODE_AUTO 0
#define NRF5_MODE_USB 1
#define NRF5_MODE_BLE 2
#define NRF5_MODE_ESB 3
#define BLE_CONN_HANDLE_INVALID 0xffff
#define POWER_SAVE_TIMEOUT 2000
#define OUTPUT_NRF5_ESB 3
#define NRF_LOG_INFO(...) (void)0
static bool ble_disable,ble_manual_stop,ble_pair_pending,ble_pairing_open,automatic_sleep_pending;
static bool route_allowed=true,power_idle=true,flash;
static uint16_t m_conn_handle=BLE_CONN_HANDLE_INVALID,power_save_counter;
static uint8_t keyboard_ble_led_stats=2;
static unsigned advertised,off_calls,light_calls;
static struct {uint8_t mode;} settings={NRF5_MODE_BLE};
static bool nrf5_output_ble_allowed(void){return route_allowed;}
static const __typeof__(settings) *nrf5_esb_settings(void){return &settings;}
static bool nrf5_esb_settings_pending(void){return false;}
static bool nrf5_pair_busy(void){return false;}
static bool nrf5_eeprom_pending(void){return flash;}
static bool nrf5_usb_powered(void){return false;}
static uint8_t where_to_send(void){return settings.mode;}
static bool nrf5_wireless_idle(void){return power_idle;}
static void advertising_start(bool erase){assert(!erase && route_allowed && m_conn_handle==BLE_CONN_HANDLE_INVALID);if(ble_disable){advertised++;ble_disable=false;}}
static void request_automatic_sleep(void){automatic_sleep_pending=true;}
static void sleep_mode_enter(void){off_calls++;}
static void rgblight_disable_noeeprom(void){light_calls++;}
''' + functions + '\nstatic void adv_idle(void){' + idle + '}\nstatic void disconnected(void){do{' + disconnected.replace('break;  // BLE_GAP_EVT_DISCONNECTED','') + '}while(0);}\n' + r'''
int main(void){
 // Reproduce an exhausted advertiser while the route remains BLE. R16 left
 // ble_disable=false, preventing advertising_start() from ever restarting.
 ble_pairing_open=true;adv_idle();assert(ble_disable && !ble_pairing_open);
 for(unsigned seconds=0;seconds<3600;seconds++){
  if(seconds%2==0){power_save_counter++;automatic_sleep_pending=true;}
  nrf5_power_task();nrf5_ble_reconnect_task();
 }
 assert(!off_calls && !light_calls && !advertised && !automatic_sleep_pending);
 // A GPIO/real input wake clears idle, without VBUS or an output mode change.
 power_idle=false;nrf5_ble_reconnect_task();assert(advertised==1 && !ble_disable);
 for(unsigned i=0;i<20;i++){nrf5_ble_reconnect_task();}
 assert(advertised==1);
 m_conn_handle=7;keyboard_ble_led_stats=2;disconnected();
 assert(m_conn_handle==BLE_CONN_HANDLE_INVALID && ble_disable && !keyboard_ble_led_stats && advertised==1);
 nrf5_ble_reconnect_task();assert(advertised==2 && !ble_disable);
 // A user DISC is distinct from remote loss and must remain stopped.
 ble_disable=true;ble_manual_stop=true;nrf5_ble_reconnect_task();assert(advertised==2);
 ble_manual_stop=false;
 // Do not interfere with explicit pairing or the 200ms delayed mode switch.
 ble_disable=true;ble_pair_pending=true;nrf5_ble_reconnect_task();assert(advertised==2);
 ble_pair_pending=false;settings.mode=NRF5_MODE_USB;nrf5_ble_reconnect_task();assert(advertised==2);
 settings.mode=NRF5_MODE_ESB;nrf5_ble_reconnect_task();assert(advertised==2);
 settings.mode=NRF5_MODE_AUTO;route_allowed=false;nrf5_ble_reconnect_task();assert(advertised==2);
 route_allowed=true;flash=true;nrf5_ble_reconnect_task();assert(advertised==2);
 flash=false;nrf5_ble_reconnect_task();assert(advertised==3);
 m_conn_handle=7;ble_disable=true;nrf5_ble_reconnect_task();assert(advertised==3);
 assert(!off_calls && !light_calls);
}
''',flags=['-DNRF5_ESB_TX','-DNRF5_ESB_ENABLE','-DNRF5_WIRELESS_POWER_ENABLE','-DRGBLIGHT_ENABLE'])

    def test_non_esb_restart_respects_selected_output(self):
        service=(helpers.ROOT/'tmk_core/protocol/nrf5/ble/ble_service.c').read_text()
        start=service.index('void nrf5_ble_reconnect_task(void) {')
        end=service.index('\n}\n#endif',start)+2
        helpers.EsbTests.run_c(self,r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define BLE_CONN_HANDLE_INVALID 0xffff
#define OUTPUT_BLUETOOTH 2
static uint16_t m_conn_handle=BLE_CONN_HANDLE_INVALID;
static bool ble_disable=true,ble_manual_stop,idle,flash;
static uint8_t output=1;
static unsigned advertised;
static uint8_t where_to_send(void){return output;}
static bool nrf5_wireless_idle(void){return idle;}
static bool nrf5_eeprom_pending(void){return flash;}
static void advertising_start(bool erase){assert(!erase);advertised++;ble_disable=false;}
''' + service[start:end] + r'''
int main(void){
 nrf5_ble_reconnect_task();assert(!advertised);
 output=0;nrf5_ble_reconnect_task();assert(!advertised);
 output=2;idle=true;nrf5_ble_reconnect_task();assert(!advertised);
 idle=false;flash=true;nrf5_ble_reconnect_task();assert(!advertised);
 flash=false;nrf5_ble_reconnect_task();assert(advertised==1);
 nrf5_ble_reconnect_task();assert(advertised==1);
}
''',flags=['-DNRF5_WIRELESS_POWER_ENABLE'])
