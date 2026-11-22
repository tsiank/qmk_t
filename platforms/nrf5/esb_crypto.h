// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "esb_packet.h"
// All crypto uses S140's ECB SVC, in thread mode only.
bool nrf5_esb_seal(const uint8_t key[16], uint64_t challenge, uint8_t *packet, uint8_t length);
bool nrf5_esb_open(const uint8_t key[16], uint64_t challenge, uint8_t *packet, uint8_t length);
bool nrf5_esb_ack_seal(const uint8_t key[16], const uint8_t *packet, uint64_t challenge, uint8_t leds, bool accepted, uint8_t ack[NRF5_ESB_ACK_SIZE]);
bool nrf5_esb_ack_open(const uint8_t key[16], const uint8_t *packet, const uint8_t ack[NRF5_ESB_ACK_SIZE], uint64_t *challenge, uint8_t *leds, bool *accepted);
