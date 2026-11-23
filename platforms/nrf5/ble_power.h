// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ble.h"
// R16's extra peripheral latency is disabled until long-idle Windows wake is
// validated. Keep baseline HID parameters in both activity states by default.
#ifndef NRF5_BLE_DYNAMIC_IDLE_ENABLE
#    define NRF5_BLE_DYNAMIC_IDLE_ENABLE 0
#endif
void nrf5_ble_power_event(const ble_evt_t *event);
void nrf5_ble_power_task(void);
