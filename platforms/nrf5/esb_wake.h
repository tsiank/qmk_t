// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#if defined(NRF5_ESB_TX) && defined(NRF5_WIRELESS_POWER_ENABLE)
void nrf5_esb_wake_init(void);
void nrf5_esb_wake_begin(void);
void nrf5_esb_wake_update(void);
bool nrf5_esb_wake_pending(void);
#else
static inline void nrf5_esb_wake_init(void) {}
static inline void nrf5_esb_wake_begin(void) {}
static inline void nrf5_esb_wake_update(void) {}
static inline bool nrf5_esb_wake_pending(void) { return false; }
#endif
