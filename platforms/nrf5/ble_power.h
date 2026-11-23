// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ble.h"
void nrf5_ble_power_event(const ble_evt_t *event);
void nrf5_ble_power_task(void);
