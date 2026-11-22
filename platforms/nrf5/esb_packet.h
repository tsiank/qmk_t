// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NRF5_ESB_PAYLOAD_MAX 64
#define NRF5_ESB_REPORT_MAX 32
#define NRF5_ESB_HEADER_SIZE 16
#define NRF5_ESB_TAG_SIZE 8
#define NRF5_ESB_ACK_SIZE 30
#define NRF5_ESB_QUEUE_SIZE 32
#define NRF5_ESB_TIMEOUT_MS 300
#define NRF5_ESB_REFRESH_MS 40

enum nrf5_esb_kind {
    NRF5_ESB_KEYBOARD = 1,
    NRF5_ESB_NKRO,
    NRF5_ESB_EXTRA,
    NRF5_ESB_MOUSE,
    NRF5_ESB_HEARTBEAT,
    NRF5_ESB_HELLO,
    NRF5_ESB_BRIDGE,
    NRF5_ESB_BRIDGE_ACK,
    NRF5_ESB_DOWN_VIA,
    NRF5_ESB_DOWN_MUSIC,
    NRF5_ESB_DOWN_MIDI,
};
typedef struct {
    uint8_t kind, length;
    uint8_t data[NRF5_ESB_REPORT_MAX];
} nrf5_esb_report_t;

// Pure wire codec: no QMK/USB calls, also safe in the timeslot callback.
bool nrf5_esb_envelope(const uint8_t *packet, uint8_t length);
bool nrf5_esb_decode(const uint8_t *packet, uint8_t length, uint64_t *session, uint32_t *sequence, nrf5_esb_report_t *report);
uint8_t nrf5_esb_encode(uint8_t *packet, uint64_t session, uint32_t sequence, const nrf5_esb_report_t *report);
bool nrf5_esb_newer(uint32_t candidate, uint32_t previous);
