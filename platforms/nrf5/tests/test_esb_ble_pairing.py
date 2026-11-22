# SPDX-License-Identifier: GPL-2.0-or-later
"""The explicit Bluetooth pairing key must work from any output route."""
import unittest
import test_esb as helpers

class BlePairingTests(unittest.TestCase):
    def test_pair_key_selects_ble_waits_for_route_and_async_disconnect(self):
        service=(helpers.ROOT/'tmk_core/protocol/nrf5/ble/ble_service.c').read_text()
        begin=service.index('static void advertising_open(void)')
        end=service.index('\nvoid ble_disconnect(void)',begin)
        actual=service[begin:end]
        globals_begin=service.index('static volatile bool ble_pair_pending,')
        globals_end=service.index(';',globals_begin)+1
        permission_begin=service.index('case PM_EVT_CONN_SEC_CONFIG_REQ: {')
        permission_end=service.index('} break;',permission_begin)+len('} break;')
        permission=service[permission_begin:permission_end]
        helpers.EsbTests.run_c(self,r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define NRF_SUCCESS 0
#define NRF_ERROR_INVALID_STATE 8
#define BLE_CONN_HANDLE_INVALID 0xFFFF
#define NRF5_MODE_BLE 2
#define APP_ERROR_CHECK(result) assert((result)==NRF_SUCCESS)
typedef int ret_code_t;
static int m_advertising;
static uint16_t m_conn_handle=BLE_CONN_HANDLE_INVALID;
static bool ble_disable=true,route_ready,request_ok=true;
static unsigned requested,advertised,opened,disconnected;
static struct {uint8_t mode;} settings={3};
static const void *last_advertising;
static bool nrf5_output_ble_allowed(void){return route_ready;}
static bool nrf5_output_request(uint8_t mode){requested++;assert(mode==2);if(request_ok)settings.mode=mode;return request_ok;}
static const __typeof__(settings) *nrf5_esb_settings(void){return &settings;}
static void advertising_start(bool erase){assert(!erase&&route_ready);advertised++;ble_disable=false;}
static int ble_advertising_restart_without_whitelist(const void *p){assert(route_ready&&m_conn_handle==BLE_CONN_HANDLE_INVALID);last_advertising=p;opened++;return 0;}
typedef struct {bool allow_repairing;} pm_conn_sec_config_t;
static bool repair_allowed;
static void pm_conn_sec_config_reply(uint16_t handle,const pm_conn_sec_config_t *config){assert(handle==9);repair_allowed=config->allow_repairing;}
static void ble_disconnect(void){assert(m_conn_handle!=BLE_CONN_HANDLE_INVALID);disconnected++;ble_disable=true;}
''' + service[globals_begin:globals_end] + '\n' + actual + '\nstatic void permission_request(void){struct {uint16_t conn_handle;} event={9};const __typeof__(event) *p_evt=&event;switch(1){' + permission.replace('PM_EVT_CONN_SEC_CONFIG_REQ','1') + '}}\n' + r'''
int main(void){
 permission_request();assert(!repair_allowed);
 // RF output cannot suppress a user's explicit Bluetooth discovery request.
 advertising_without_whitelist();assert(requested==1&&settings.mode==2&&ble_pair_pending&&!opened);
 for(int i=0;i<4;i++){nrf5_ble_pairing_task();}assert(!opened&&!advertised);
 route_ready=true;nrf5_ble_pairing_task();assert(opened==1&&advertised==1&&ble_pairing_open&&!ble_pair_pending);
 assert(last_advertising==&m_advertising);permission_request();assert(repair_allowed);
 nrf5_ble_pairing_task();assert(opened==1); // execute once, not every scan
 // Connected hosts must finish GAP disconnection before opening discovery.
 m_conn_handle=7;advertising_without_whitelist();nrf5_ble_pairing_task();
 assert(disconnected==1&&opened==1&&ble_pair_pending);
 for(int i=0;i<5;i++){nrf5_ble_pairing_task();}assert(disconnected==1&&opened==1);
 m_conn_handle=BLE_CONN_HANDLE_INVALID;nrf5_ble_pairing_task();assert(opened==2&&ble_pairing_open);
 // A later explicit mode choice cancels a queued pairing request.
 advertising_without_whitelist();settings.mode=1;nrf5_ble_pairing_task();
 assert(!ble_pair_pending&&!ble_pairing_open&&opened==2);permission_request();assert(!repair_allowed);
 // Flash-save/pairing rejection must not queue an action on another host.
 request_ok=false;advertising_without_whitelist();assert(!ble_pair_pending&&opened==2);
}
''',flags=['-DNRF5_ESB_TX','-DNRF5_ESB_ENABLE'])

if __name__=='__main__':unittest.main()
