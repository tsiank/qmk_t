/*
Copyright 2017 Priyadi Iman Nurcahyo
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "outputselect.h"

#include "host.h"
#include "nrf5_usb.h"
#include "ble_service.h"
#include "ble_hids_service.h"
#include "keycodes.h"
#ifdef NRF5_ESB_ENABLE
#    include "nrf5_esb.h"
#    include "esb_settings.h"
#    include "esb_pair.h"
#    include "nrf5_power.h"
#    include "timer.h"
#    include "action_util.h"
#endif

#ifdef MODULE_ADAFRUIT_BLE
#    include "adafruit_ble.h"
#endif

uint8_t desired_output = OUTPUT_DEFAULT;

bool process_nrf5_output(uint16_t keycode, keyrecord_t *record) {
#ifdef NRF5_ESB_ENABLE
    if (keycode == KC_ESCAPE && nrf5_pair_busy()) {
        if (record->event.pressed) (void)nrf5_pair_cancel();
        return false;
    }
#endif
    uint8_t output;
    switch (keycode) {
#ifdef NRF5_ESB_ENABLE
        case QK_OUTPUT_2P4GHZ:
            nrf5_pair_key(record->event.pressed);
#ifdef NRF5_ESB_TX
            if (record->event.pressed) nrf5_output_request(NRF5_MODE_ESB);
#endif
            return false;
        case QK_OUTPUT_AUTO:
#ifdef NRF5_ESB_TX
            if (record->event.pressed) nrf5_output_request(NRF5_MODE_AUTO);
#endif
            return false;
#endif
#ifdef NRF5_ESB_TX
        case QK_OUTPUT_NEXT:
        case QK_OUTPUT_PREV:
            if (record->event.pressed) {
                uint8_t mode = nrf5_output_mode();
                mode = keycode == QK_OUTPUT_NEXT ? mode >= NRF5_MODE_ESB ? NRF5_MODE_USB : mode + 1 : mode <= NRF5_MODE_USB ? NRF5_MODE_ESB : mode - 1;
                nrf5_output_request(mode);
            }
            return false;
#endif
        case QK_OUTPUT_USB:
            output = OUTPUT_USB;
            break;
        case QK_OUTPUT_BLUETOOTH:
            output = OUTPUT_BLUETOOTH;
            break;
        case QK_OUTPUT_NONE:
            output = OUTPUT_NONE;
            break;
        default:
            return true;
    }
    if (record->event.pressed) {
#ifdef NRF5_ESB_TX
        nrf5_output_request(output == OUTPUT_USB ? NRF5_MODE_USB : output == OUTPUT_BLUETOOTH ? NRF5_MODE_BLE : NRF5_MODE_NONE);
#else
        set_output(output);
#endif
    }
    return false;
}

// QMK's USB-only default cannot describe the active nRF5 dual-mode driver.
bool host_can_send_nkro(void) {
#ifdef NKRO_ENABLE
#    ifdef NRF5_ESB_TX
    if (host_get_driver() == nrf5_esb_driver()) return true;
#    endif
    if (host_get_driver() == nrf5_usb_driver()) {
        return keyboard_protocol != 0;
    }
    if (host_get_driver() == nrf5_ble_driver()) {
        return nrf5_ble_can_send_nkro();
    }
#endif
    return false;
}

/** \brief Set Output
 *
 * FIXME: Needs doc
 */
void set_output(uint8_t output) {
#ifdef NRF5_ESB_RX
    host_set_driver(nrf5_esb_driver());
    desired_output = OUTPUT_NRF5_ESB;
    set_output_user(OUTPUT_NRF5_ESB);
    (void)output;
#elif defined(NRF5_ESB_TX)
    // USB power/BLE callbacks announce availability. Manual preference is
    // persistent and can only be changed by an explicit key/client request.
    (void)output;
#else
    switch (output) {
        case OUTPUT_NONE:
            host_set_driver(0);
            break;
        case OUTPUT_USB:
            host_set_driver(nrf5_usb_driver());
            // host_set_driver(0);
            break;
        case OUTPUT_BLUETOOTH:
            host_set_driver(nrf5_ble_driver());
            break;
    }
    set_output_user(output);
    desired_output = output;
#endif
}

#ifdef NRF5_ESB_ENABLE
static uint8_t current_mode = NRF5_MODE_NONE;
#ifdef NRF5_ESB_TX
static bool switching;
static uint32_t switch_started, requested_at;
static bool request_delay;
#endif
uint8_t nrf5_output_mode(void) { return current_mode; }
bool nrf5_output_ble_allowed(void) {
#ifdef NRF5_ESB_TX
    return current_mode == NRF5_MODE_BLE;
#else
    return false;
#endif
}
bool nrf5_output_request(uint8_t mode) {
#ifdef NRF5_ESB_RX
    return mode == NRF5_MODE_USB;
#else
    if (nrf5_pair_busy() && !nrf5_pair_cancel()) return false;
    if (!nrf5_esb_settings_mode(mode)) return false;
    requested_at = nrf5_esb_now(); request_delay = true;
    return true;
#endif
}
void nrf5_output_task(void) {
#ifdef NRF5_ESB_TX
    uint32_t now = nrf5_esb_now();
    if (request_delay && now - requested_at < 200) return;
    request_delay = false;
    bool pairing = nrf5_pair_busy();
    uint8_t mode = pairing ? NRF5_MODE_NONE : nrf5_esb_settings()->mode;
    nrf5_esb_set_probe(mode == NRF5_MODE_AUTO && !nrf5_usb_powered() && nrf5_esb_settings()->paired);
    if (mode == NRF5_MODE_AUTO) mode = nrf5_usb_powered() ? NRF5_MODE_USB : nrf5_esb_connected() ? NRF5_MODE_ESB : NRF5_MODE_BLE;
    if (mode == current_mode && !switching) return;
    if (!switching) {
        // Release on the old host first. Suppress input during the transition
        // while keeping local VIA replies available through the quiet driver.
        clear_keyboard(); host_set_driver(nrf5_esb_quiet_driver());
        if (current_mode == NRF5_MODE_ESB) nrf5_esb_release();
        switching = true; switch_started = now;
    }
    if (current_mode == NRF5_MODE_ESB && !nrf5_esb_idle() && now - switch_started < 100) return;
    nrf5_esb_set_active(mode == NRF5_MODE_ESB);
    if (current_mode == NRF5_MODE_BLE && mode != NRF5_MODE_BLE) ble_disconnect();
    current_mode = mode; switching = false;
    if (mode == NRF5_MODE_USB) { desired_output = OUTPUT_USB; host_set_driver(nrf5_usb_driver()); }
    else if (mode == NRF5_MODE_BLE) { desired_output = OUTPUT_BLUETOOTH; host_set_driver(nrf5_ble_driver()); advertising_start(false); }
    else if (mode == NRF5_MODE_ESB) { desired_output = OUTPUT_NRF5_ESB; host_set_driver(nrf5_esb_driver()); }
    else { desired_output = OUTPUT_NONE; host_set_driver(nrf5_esb_quiet_driver()); }
    set_output_user(desired_output);
#endif
}
void nrf5_output_init(void) {
#ifdef NRF5_ESB_RX
    current_mode = NRF5_MODE_USB; set_output(OUTPUT_NRF5_ESB);
#else
    host_set_driver(nrf5_esb_quiet_driver());
    desired_output = OUTPUT_NONE;
    nrf5_output_task();
#endif
}
#endif

/** \brief Set Output User
 *
 * FIXME: Needs doc
 */
__attribute__((weak)) void set_output_user(uint8_t output) { (void)output; }

/** \brief Auto Detect Output
 *
 * FIXME: Needs doc
 */
// uint8_t auto_detect_output(void) {
//     if (USB_DeviceState == DEVICE_STATE_Configured) {
//         return OUTPUT_USB;
//     }

// #ifdef MODULE_ADAFRUIT_BLE
//     if (adafruit_ble_is_connected()) {
//         return OUTPUT_BLUETOOTH;
//     }
// #endif

// #ifdef BLUETOOTH_ENABLE
//     return OUTPUT_BLUETOOTH; // should check if BT is connected here
// #endif

//     return OUTPUT_NONE;
// }

/** \brief Where To Send
 *
 * FIXME: Needs doc
 */
uint8_t where_to_send(void) {
    // if (desired_output == OUTPUT_AUTO) {
    //     return auto_detect_output();
    // }
    return desired_output;
}
