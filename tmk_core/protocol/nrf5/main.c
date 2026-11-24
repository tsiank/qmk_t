/**

 * Copyright (c) 2017 - 2019, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef NRF5_CONFIG_ENABLE
#include "nrf5_features.h"
#endif
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "nrf5_usb.h"
#include "ble_service.h"
#include "ble.h"

#include "keyboard.h"
#include "eeprom.h"
#include "nrf5_power.h"
#ifdef NRF5_WIRELESS_POWER_ENABLE
#    include "wireless_power.h"
#    include "ble_power.h"
#endif
#ifdef NRF5_ESB_ENABLE
#    include "nrf5_esb.h"
#    include "esb_wake.h"
#    include "outputselect.h"
#endif
#ifdef NRF5_OLED_STATUS_ENABLE
#    include "nrf5_ui.h"
#endif
#ifdef OLED_ENABLE
#include "oled_driver.h"
#include "timer.h"
#endif
#ifdef RAW_ENABLE
#include "via_transport.h"
#endif
#ifdef MIDI_ENABLE
#include "qmk_midi.h"
#include "midi_transport.h"
#endif

#ifdef AUDIO_ENABLE
#include "audio_pwm.h"
#endif

uint16_t m_conn_handle     = BLE_CONN_HANDLE_INVALID; /**< Handle of the current connection. */
uint8_t  keyboard_protocol = 1;

APP_TIMER_DEF(m_keyboard_scan_timer); /**< keyboard scan timer. */

#ifndef KEYBOARD_SCAN_INTERVAL
#    define KEYBOARD_SCAN_INTERVAL 4
#endif

static volatile bool keyboard_scan_pending;

static void keyboard_scan_handler(void* p_context) {
    UNUSED_PARAMETER(p_context);
    // I2C waits call sd_app_evt_wait(), which returns to thread mode. The
    // current QMK lighting/storage code must not execute inside app_timer IRQs.
    keyboard_scan_pending = true;
}

#ifdef NRF5_WIRELESS_POWER_ENABLE
void nrf5_keyboard_scan_wake(void) { keyboard_scan_pending = true; }
#endif

static void log_init(void) {
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void timers_init(void) {
    ret_code_t err_code;
    err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}

int main(void) {
    keyboard_setup();
    keyboard_init();

    ret_code_t ret;

    log_init();
    timers_init();

    usb_keyboard_init();
    ble_service_init();
#ifdef NRF5_ESB_ENABLE
    nrf5_esb_init();
    nrf5_output_init();
    nrf5_esb_wake_init();
#endif

    #ifdef MIDI_ENABLE
        setup_midi();
    #endif

    ret = app_timer_create(&m_keyboard_scan_timer, APP_TIMER_MODE_REPEATED, keyboard_scan_handler);
    APP_ERROR_CHECK(ret);
    ret = app_timer_start(m_keyboard_scan_timer, APP_TIMER_TICKS(KEYBOARD_SCAN_INTERVAL), NULL);
    APP_ERROR_CHECK(ret);

#ifdef OLED_ENABLE
    bool display_ready = false;
    bool display_attempted = false;
    uint32_t display_attempt = timer_read32();
#endif

    if (!nrf5_usb_powered()) {
        advertising_start(false);
    }

#ifdef NRF5_WIRELESS_POWER_ENABLE
    uint32_t scan_interval = KEYBOARD_SCAN_INTERVAL;
#endif
    while (true) {
        while (app_usbd_event_queue_process()) {
            /* Nothing to do */
        }
#ifdef NRF5_ESB_ENABLE
        nrf5_esb_wake_begin();
        nrf5_esb_task();
        nrf5_output_task();
#ifdef NRF5_ESB_TX
        nrf5_ble_pairing_task();
#endif
#endif
#ifdef MIDI_ENABLE
        nrf_midi_task();
#endif
#ifdef RAW_ENABLE
        nrf_via_task();
#endif
#ifdef EXTRAKEY_ENABLE
        nrf5_usb_extra_task();
#endif
#ifdef NRF5_WIRELESS_POWER_ENABLE
        nrf5_wireless_power_task();
        nrf5_battery_task();
        nrf5_ble_reconnect_task();
        nrf5_ble_power_task();
#endif
        const bool scanned = keyboard_scan_pending;
        if (scanned) {
            keyboard_scan_pending = false;
#ifdef NRF5_WIRELESS_POWER_ENABLE
            nrf5_wireless_prepare_scan();
#endif
            keyboard_task();
#ifdef AUDIO_ENABLE
            nrf_audio_task();
#endif
            eeprom_update();
            housekeeping_task();
            nrf5_power_task();
#ifdef OLED_ENABLE
            // Drain USB events before any display I/O; allow enumeration to
            // start before the first OLED attempt. Subsequent retries are slow.
            if (!display_ready && timer_elapsed32(display_attempt) >= (display_attempted ? 1000 : 250)) {
                display_attempted = true;
                display_ready = oled_init(OLED_ROTATION_0);
                display_attempt = timer_read32();
            }
#endif
#ifdef NRF5_OLED_STATUS_ENABLE
            nrf5_oled_power_task();
#endif
#ifdef NRF5_CONFIG_ENABLE
            nrf5_features_task();
#endif
#ifdef NRF5_WIRELESS_POWER_ENABLE
            nrf5_wireless_power_task();
            nrf5_wireless_finish_scan();
#endif
        }
#ifdef NRF5_WIRELESS_POWER_ENABLE
        uint32_t desired_interval = nrf5_wireless_scan_interval();
        if (desired_interval != scan_interval) {
            ret = app_timer_stop(m_keyboard_scan_timer);
            APP_ERROR_CHECK(ret);
            ret = app_timer_start(m_keyboard_scan_timer, APP_TIMER_TICKS(desired_interval), NULL);
            APP_ERROR_CHECK(ret);
            scan_interval = desired_interval;
        }
#endif
#ifdef NRF5_ESB_ENABLE
        nrf5_esb_wake_update();
        if (nrf5_esb_wake_pending()) continue;
#endif
        // Restore the fast timer before sleeping when USB, music or a GPIO
        // interrupt wakes standby. Keep processing queued reports after scans.
        if (!scanned && !keyboard_scan_pending) idle_state_handle();
    }
}
