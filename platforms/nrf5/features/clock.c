// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_clock.h"
#include "app_timer.h"

static bool synchronized;
static uint32_t daytime_ms;
static uint32_t last_tick;
static uint32_t tick_fraction;

void nrf5_clock_set(uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisecond) {
    if (hour >= 24 || minute >= 60 || second >= 60 || millisecond >= 1000) return;
    daytime_ms = ((uint32_t)hour * 3600 + (uint32_t)minute * 60 + second) * 1000 + millisecond;
    last_tick = app_timer_cnt_get();
    tick_fraction = 0;
    synchronized = true;
}
void nrf5_clock_task(void) {
    if (!synchronized) return;
    uint32_t now = app_timer_cnt_get();
    // Use actual 32768 Hz RTC ticks, not QMK's approximate 32-ticks/ms timer.
    uint64_t scaled = (uint64_t)app_timer_cnt_diff_compute(now, last_tick) * 1000 + tick_fraction;
    last_tick = now;
    daytime_ms = (daytime_ms + scaled / 32768) % 86400000;
    tick_fraction = scaled % 32768;
}
bool nrf5_clock_read(uint8_t *hour, uint8_t *minute, uint8_t *second) {
    nrf5_clock_task();
    if (!synchronized) return false;
    uint32_t seconds = daytime_ms / 1000;
    *hour = seconds / 3600;
    *minute = (seconds / 60) % 60;
    *second = seconds % 60;
    return true;
}
uint8_t nrf5_clock_branch(uint8_t hour) { return ((hour + 1) / 2) % 12; }
bool nrf5_clock_is_zheng(uint8_t hour) { return hour % 2 == 0; }
uint8_t nrf5_clock_quarter(uint8_t minute) { return minute / 15; }
