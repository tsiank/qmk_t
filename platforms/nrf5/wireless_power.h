// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef KEYBOARD_SCAN_INTERVAL
#    define KEYBOARD_SCAN_INTERVAL 4
#endif
#ifndef NRF5_WIRELESS_IDLE_TIMEOUT
#    define NRF5_WIRELESS_IDLE_TIMEOUT 30000
#endif
#ifndef NRF5_WIRELESS_IDLE_SCAN_INTERVAL
#    define NRF5_WIRELESS_IDLE_SCAN_INTERVAL 1000
#endif
void nrf5_wireless_power_task(void);
void nrf5_wireless_note_activity(void);
bool nrf5_wireless_idle(void);
void nrf5_wireless_prepare_scan(void);
void nrf5_wireless_finish_scan(void);
void nrf5_wireless_before_system_off(void);
uint32_t nrf5_wireless_scan_interval(void);
// Main-loop scheduler hook. The GPIO IRQ only disables sense and queues work.
void nrf5_keyboard_scan_wake(void);
