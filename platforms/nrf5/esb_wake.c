// SPDX-License-Identifier: GPL-2.0-or-later
#include "esb_wake.h"
#include "nrf5_esb.h"
#include "esb_settings.h"
#include "esb_packet.h"
#include "wireless_power.h"
#include "app_timer.h"
#include "outputselect.h"
#include "app_error.h"
#include "app_util_platform.h"
#include "nrf.h"
#include "timer.h"

// GPIO standby scans only once per second. RF supervision is 300 timer ticks:
// wake the main loop separately, without scanning the matrix or lighting up.
APP_TIMER_DEF(service_timer);
static volatile bool running;
static volatile bool pending;
static uint32_t armed_at, armed_delay;
static void wake(void *context) { (void)context; running = false; pending = true; __SEV(); }
void nrf5_esb_wake_init(void) {
    APP_ERROR_CHECK(app_timer_create(&service_timer, APP_TIMER_MODE_SINGLE_SHOT, wake));
}
void nrf5_esb_wake_begin(void) { pending = false; }
bool nrf5_esb_wake_pending(void) { return pending; }
static void update_timer(void) {
    bool needed = nrf5_output_mode() == NRF5_MODE_ESB &&
        nrf5_wireless_scan_interval() > NRF5_ESB_REFRESH_MS;
    if (!needed) {
        if (running) APP_ERROR_CHECK(app_timer_stop(service_timer));
        running = false;
        return;
    }
    uint32_t delay = nrf5_esb_next_wake_ticks();
    uint32_t now = timer_read32() & 0x7FFFFu;
    uint32_t elapsed = (now - armed_at) & 0x7FFFFu;
    // Unrelated USB/RTC IRQs must not postpone an already earlier deadline.
    if (running && (elapsed >= armed_delay || armed_delay - elapsed <= delay)) return;
    if (running) APP_ERROR_CHECK(app_timer_stop(service_timer));
    armed_at = now; armed_delay = delay;
    // QMK uses 1024 Hz, APP_TIMER_TICKS takes milliseconds. Round up so a
    // timer never repeatedly fires just before the transport deadline.
    uint32_t milliseconds = ((uint64_t)delay * 1000 + 1023) / 1024;
    running = true;
    APP_ERROR_CHECK(app_timer_start(service_timer, APP_TIMER_TICKS(milliseconds), NULL));
}
void nrf5_esb_wake_update(void) {
    // The single-shot callback changes running; publish the new timer state
    // atomically so an expiry cannot be lost between testing and rearming.
    CRITICAL_REGION_ENTER();
    update_timer();
    CRITICAL_REGION_EXIT();
}
