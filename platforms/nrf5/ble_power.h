// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ble.h"
// Boards opt in after checking their host's idle/wake behavior. Other boards
// keep the baseline HID parameters in both activity states by default.
#ifndef NRF5_BLE_DYNAMIC_IDLE_ENABLE
#    define NRF5_BLE_DYNAMIC_IDLE_ENABLE 0
#endif
void nrf5_ble_power_event(const ble_evt_t *event);
void nrf5_ble_power_task(void);
