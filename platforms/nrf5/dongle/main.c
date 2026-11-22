// SPDX-License-Identifier: GPL-2.0-or-later
// Dedicated USB receiver: no local QMK keyboard/matrix, display or BLE HID.
// USB proxies keyboard configuration/music and exposes class-compliant MIDI.
#include "nrf.h"
#include "nrf_sdh.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"
#include "app_timer.h"
#include "app_usbd.h"
#include "app_error.h"
#include "nrf5_esb.h"

void nrf5_dongle_usb_init(void);
void nrf5_dongle_usb_task(void);
APP_TIMER_DEF(dongle_tick);
static void tick(void *context) { (void)context; }
int main(void) {
    APP_ERROR_CHECK(nrf_drv_clock_init());
    // SDK 15 nrf_drv_power_init returns NRF_ERROR_INVALID_STATE after the
    // SoftDevice is enabled. app_usbd_init accepts this already-initialized
    // power driver; its later USB event setup uses the SoftDevice SoC API.
    APP_ERROR_CHECK(nrf_drv_power_init(NULL));
    APP_ERROR_CHECK(nrf_sdh_enable_request());
    APP_ERROR_CHECK(app_timer_init());
    // Keep RTC1 running even when unpaired and USB has no traffic. Timeouts,
    // Flash retries and the delayed DFU reset must keep advancing.
    APP_ERROR_CHECK(app_timer_create(&dongle_tick, APP_TIMER_MODE_REPEATED, tick));
    APP_ERROR_CHECK(app_timer_start(dongle_tick, APP_TIMER_TICKS(10), NULL));
    nrf5_dongle_usb_init();
    nrf5_esb_init();
    while (true) {
        while (app_usbd_event_queue_process()) {}
        nrf5_esb_task();
        nrf5_dongle_usb_task();
        (void)sd_app_evt_wait();
    }
}
