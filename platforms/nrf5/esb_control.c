// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_esb.h"
#include "esb_settings.h"
#include "esb_pair.h"
#ifndef NRF5_ESB_DONGLE
#    include "outputselect.h"
#else
static bool nrf5_output_request(uint8_t mode) { return mode == NRF5_MODE_USB; }
static uint8_t nrf5_output_mode(void) { return NRF5_MODE_USB; }
#endif
#include "nrf.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t value) {
    for (uint8_t b = 0; b < 4; b++) p[b] = value >> (24 - b * 8);
}
void nrf5_esb_command(uint8_t data[32], bool usb) {
    uint8_t command = data[1], result = 0;
    uint8_t sequence[2] = {data[30], data[31]};
    bool valid = data[2] == 1 && command <= 6;
    for (uint8_t i = command == 2 ? 24 : 4; i < 30; i++) valid &= data[i] == 0;
    if (command != 1 && data[3]) valid = false;
    if (!valid) result = 1;
    else if (command >= 2 && !usb) result = 2; // Receiver setup is local/physical.
    else if (command == 1) {
        if (!nrf5_output_request(data[3])) result = 3;
    } else if (command == 2) {
        uint32_t address = (uint32_t)data[20] << 24 | (uint32_t)data[21] << 16 | (uint32_t)data[22] << 8 | data[23];
        if (data[3] || nrf5_pair_busy() || !nrf5_esb_settings_pair(data + 4, address)) result = 3;
    } else if (command == 3) {
        if (nrf5_pair_busy()) result = 3;
        else nrf5_esb_settings_clear();
    } else if (command == 4) { if (!nrf5_pair_start()) result = 3; }
    else if (command == 5) { if (!nrf5_pair_confirm()) result = 3; }
    else if (command == 6) { if (!nrf5_pair_cancel()) result = 3; }
    const nrf5_esb_settings_t *s = nrf5_esb_settings();
    memset(data + 1, 0, 31);
    data[1] = 2; data[2] = result;
#ifdef NRF5_ESB_TX
    data[3] = 1;
#else
    data[3] = 2;
#endif
    data[4] = s->mode; data[5] = nrf5_output_mode(); data[6] = s->paired;
    data[7] = nrf5_esb_settings_pending() ? 0 : nrf5_esb_settings_error() ? 2 : 1;
    data[8] = nrf5_esb_connected(); put32(data + 9, s->address);
    put32(data + 13, NRF_FICR->DEVICEID[0]); put32(data + 17, NRF_FICR->DEVICEID[1]);
    put32(data + 21, nrf5_esb_settings_error());
    data[25] = sequence[0]; data[26] = sequence[1];
    data[27] = nrf5_pair_state(); data[28] = nrf5_pair_seconds();
    uint32_t fingerprint = nrf5_pair_fingerprint();
    data[29] = fingerprint >> 16; data[30] = fingerprint >> 8; data[31] = fingerprint;
}
