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

#ifdef MODULE_ADAFRUIT_BLE
#    include "adafruit_ble.h"
#endif

uint8_t desired_output = OUTPUT_DEFAULT;

bool process_nrf5_output(uint16_t keycode, keyrecord_t *record) {
    uint8_t output;
    switch (keycode) {
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
        set_output(output);
    }
    return false;
}

// QMK's USB-only default cannot describe the active nRF5 dual-mode driver.
bool host_can_send_nkro(void) {
#ifdef NKRO_ENABLE
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
}

/** \brief Set Output User
 *
 * FIXME: Needs doc
 */
__attribute__((weak)) void set_output_user(uint8_t output) {}

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
