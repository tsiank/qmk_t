// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "esb_packet.h"
#define NRF5_BRIDGE_VIA 1
#define NRF5_BRIDGE_CONTROL 2
#define NRF5_BRIDGE_MIDI 3
void nrf5_bridge_reset(void);
void nrf5_bridge_abort(void);
void nrf5_bridge_tick(void);
// Service hooks run exclusively in the main loop. False means backpressure.
bool nrf5_bridge_down_deliver(uint8_t kind, const uint8_t *data, uint8_t length);
bool nrf5_bridge_up_deliver(uint8_t channel, uint16_t token, const uint8_t *data, uint8_t length);
void nrf5_bridge_link_reset(void);
#ifdef NRF5_ESB_TX
bool nrf5_bridge_fast_poll(void);
bool nrf5_bridge_send(uint8_t channel, uint16_t token, const uint8_t *data, uint8_t length);
bool nrf5_bridge_next(nrf5_esb_report_t *report);
void nrf5_bridge_sent(void);
void nrf5_bridge_down(const uint8_t extension[34], const uint8_t key[16], uint64_t session, uint64_t challenge);
#else
bool nrf5_bridge_submit(uint8_t kind, const uint8_t *data, uint8_t length, bool latest);
bool nrf5_bridge_via_request(uint16_t token, const uint8_t data[32]);
bool nrf5_bridge_up_ready(void);
void nrf5_bridge_up(const nrf5_esb_report_t *report);
void nrf5_bridge_confirm(uint32_t sequence);
bool nrf5_bridge_extension(uint8_t extension[34], const uint8_t key[16], uint64_t session, uint64_t challenge);
#endif
