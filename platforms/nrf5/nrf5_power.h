// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include "nrf.h"

// VBUS can already be present while the queued USB power events have not yet
// enabled USBD. An active peripheral also keeps automatic system-off blocked.
static inline bool nrf5_usb_powered(void) {
    return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0 || NRF_USBD->ENABLE != 0;
}
void nrf5_power_task(void);
