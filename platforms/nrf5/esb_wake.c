// SPDX-License-Identifier: GPL-2.0-or-later
#include "esb_wake.h"
#include "nrf5_esb.h"
#include "esb_settings.h"
#include "esb_packet.h"
#include "wireless_power.h"
#include "app_timer.h"
#include "outputselect.h"
#include "app_error.h"
#include "nrf.h"

// GPIO standby scans only once per second. RF supervision is 300 timer ticks:
// wake the main loop separately, without scanning the matrix or lighting up.
APP_TIMER_DEF(service_timer);
static bool running;
static volatile bool pending;
static void wake(void *context) { (void)context; pending = true; __SEV(); }
void nrf5_esb_wake_init(void) {
    APP_ERROR_CHECK(app_timer_create(&service_timer, APP_TIMER_MODE_REPEATED, wake));
}
void nrf5_esb_wake_begin(void) { pending = false; }
bool nrf5_esb_wake_pending(void) { return pending; }
void nrf5_esb_wake_update(void) {
    bool needed = nrf5_output_mode() == NRF5_MODE_ESB &&
        nrf5_wireless_scan_interval() > NRF5_ESB_REFRESH_MS;
    if (needed == running) return;
    if (needed) APP_ERROR_CHECK(app_timer_start(service_timer, APP_TIMER_TICKS(NRF5_ESB_REFRESH_MS), NULL));
    else APP_ERROR_CHECK(app_timer_stop(service_timer));
    running = needed;
}
