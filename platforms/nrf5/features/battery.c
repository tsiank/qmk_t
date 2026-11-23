// SPDX-License-Identifier: GPL-2.0-or-later
#include "battery.h"
#include "nrf5_power.h"
#include <string.h>
#ifdef NRF5_BATTERY_ENABLE
#    include "adc.h"
#    include "app_util_platform.h"
#endif

// Read cached samples only: querying must not restart SAADC or wake lighting.
// Bytes 28..31 are an opaque request nonce, echoed unchanged.
void nrf5_battery_command(uint8_t data[32]) {
    bool valid = data[1] == 1;
    for (unsigned i = 2; i < 28; ++i) valid &= data[i] == 0;
    if (!valid) { data[0] = 0xFF; return; }
    memset(data + 2, 0, 26);
    if (nrf5_usb_powered()) data[2] |= 4; // Supply detected, not a charge-status signal.
#ifdef NRF5_BATTERY_ENABLE
    data[2] |= 1;
    uint16_t millivolts;
    uint8_t percent;
    CRITICAL_REGION_ENTER();
    millivolts = get_vcc();
    percent = get_battery_level();
    CRITICAL_REGION_EXIT();
    if (millivolts > 0 && millivolts <= 6000 && percent <= 100) {
        data[2] |= 2;
        data[3] = percent;
        data[4] = millivolts >> 8;
        data[5] = millivolts;
    }
#endif
}
