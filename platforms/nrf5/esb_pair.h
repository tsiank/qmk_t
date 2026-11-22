// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NRF5_ESB_PAIR_ADDRESS 0xC3A596E7u
#define NRF5_ESB_PAIR_WINDOW_MS 60000u
enum { NRF5_PAIR_IDLE, NRF5_PAIR_WINDOW, NRF5_PAIR_CANDIDATE, NRF5_PAIR_CONFIRM,
       NRF5_PAIR_SAVING, NRF5_PAIR_COMPLETE, NRF5_PAIR_FAILED, NRF5_PAIR_TIMEOUT, NRF5_PAIR_CANCELLED };
enum { NRF5_PAIR_PUBLIC = 7, NRF5_PAIR_FINISH = 9 };

bool nrf5_pair_start(void);
#ifdef NRF5_ESB_DONGLE
// Physical USB insertion authorizes a short discovery window. Existing input
// continues on the private pipe until a keyboard requests a new pairing.
void nrf5_pair_usb_window(void);
#endif
bool nrf5_pair_confirm(void);
bool nrf5_pair_cancel(void);
bool nrf5_pair_busy(void);
bool nrf5_pair_task(uint32_t now);
void nrf5_pair_key(bool pressed);
void nrf5_pair_poll_key(uint32_t now);
uint8_t nrf5_pair_state(void);
uint8_t nrf5_pair_seconds(void);
uint32_t nrf5_pair_fingerprint(void);
void nrf5_pair_receive(const uint8_t *packet, uint8_t length);
static inline bool nrf5_pair_envelope(const uint8_t *p, uint8_t length) {
    return p && (length == 24 || length == 56) && p[0] == 0x51 && p[1] == 3 &&
        ((length == 56 && p[2] == NRF5_PAIR_PUBLIC && p[3] == 32) ||
         (length == 24 && p[2] == NRF5_PAIR_FINISH && p[3] == 0));
}

// Public-key exchange primitives; thread mode only, using SDK Oberon.
void nrf5_pair_public(uint8_t public_key[32], uint8_t private_key[32]);
bool nrf5_pair_derive(const uint8_t private_key[32], const uint8_t peer_public[32],
    const uint8_t tx_public[32], const uint8_t rx_public[32], uint64_t tx_nonce, uint64_t rx_nonce,
    uint8_t link_key[16], uint8_t confirm_key[16], uint32_t *address, uint32_t *fingerprint);
void nrf5_pair_sign(const uint8_t key[16], uint8_t *packet, uint8_t length);
bool nrf5_pair_verify(const uint8_t key[16], const uint8_t *packet, uint8_t length);
