// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "esb_packet.h"

void nrf5_esb_radio_init(void);
void nrf5_esb_radio_configure(bool enabled, uint32_t address);
void nrf5_esb_radio_listen_pair(bool enabled);
void nrf5_esb_radio_task(void);
bool nrf5_esb_radio_send(const uint8_t *packet, uint8_t length);
bool nrf5_esb_radio_result(bool *acknowledged);
bool nrf5_esb_radio_receive(uint8_t *packet, uint8_t *length);
void nrf5_esb_radio_ack(uint8_t ack[NRF5_ESB_ACK_SIZE]);
void nrf5_esb_radio_cache_ack(const uint8_t *packet, uint8_t length, const uint8_t ack[NRF5_ESB_ACK_SIZE]);
uint8_t nrf5_esb_radio_reply(uint8_t reply[64]);
void nrf5_esb_radio_cache_reply(const uint8_t *packet, uint8_t length, const uint8_t *reply, uint8_t reply_length);
uint32_t nrf5_esb_radio_error(void);
// slots, transmissions, valid ACKs, accepted RX packets, denied grants.
void nrf5_esb_radio_counters(uint32_t counters[5]);
