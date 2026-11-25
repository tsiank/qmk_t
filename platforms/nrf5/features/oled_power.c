// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_ui.h"
#include "nrf5_power.h"
#include "oled_driver.h"
#include "keyboard.h"

static uint8_t power_mode;
static uint32_t last_activity;
static bool idle_latched;

uint8_t nrf5_oled_get_power_mode(void) { return power_mode; }
bool oled_platform_can_wake(void) { return nrf5_oled_wake_allowed(); }
bool nrf5_oled_wake_allowed(void) {
    if (power_mode == 2) return false;
    if (power_mode == 1 || nrf5_usb_powered()) return true;
    uint32_t activity = last_input_activity_time();
    if (activity != last_activity) { last_activity = activity; idle_latched = false; }
#if OLED_TIMEOUT > 0
    // Latch timeout until actual input so QMK's short RTC wrap cannot wake us.
    if (last_input_activity_elapsed() >= OLED_TIMEOUT) idle_latched = true;
#endif
    return !idle_latched;
}
void nrf5_oled_power_task(void) {
    if (nrf5_oled_wake_allowed()) oled_on();
    else oled_off();
}
void nrf5_oled_set_power_mode(uint8_t mode) {
    if (mode > 2) return;
    power_mode = mode;
    nrf5_oled_power_task();
}
