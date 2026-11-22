// SPDX-License-Identifier: GPL-2.0-or-later
#include "esb_packet.h"
#include <string.h>

static bool valid(uint8_t kind, uint8_t length, const uint8_t *data) {
    switch (kind) {
        case NRF5_ESB_KEYBOARD: return length == 8;
        case NRF5_ESB_NKRO: return length == 31;
        case NRF5_ESB_EXTRA: return length == 3 && (!data || data[0] == 3 || data[0] == 4);
        case NRF5_ESB_MOUSE: return length == 5;
        case NRF5_ESB_HEARTBEAT: return length == 0;
        case NRF5_ESB_HELLO: return length == 0;
        case NRF5_ESB_BRIDGE: return length >= 8 && length <= 32;
        case NRF5_ESB_BRIDGE_ACK: return length == 4;
        case NRF5_ESB_DOWN_VIA: return length == 20;
        case NRF5_ESB_DOWN_MUSIC: return length == 20;
        case NRF5_ESB_DOWN_MIDI: return length == 4;
        default: return false;
    }
}
bool nrf5_esb_envelope(const uint8_t *p, uint8_t length) {
    return p && length >= NRF5_ESB_HEADER_SIZE + NRF5_ESB_TAG_SIZE &&
        length <= NRF5_ESB_HEADER_SIZE + NRF5_ESB_REPORT_MAX + NRF5_ESB_TAG_SIZE &&
        p[0] == 0x51 && p[1] == 2 && p[3] == length - NRF5_ESB_HEADER_SIZE - NRF5_ESB_TAG_SIZE && valid(p[2], p[3], NULL);
}
bool nrf5_esb_decode(const uint8_t *p, uint8_t length, uint64_t *session, uint32_t *sequence, nrf5_esb_report_t *report) {
    if (!nrf5_esb_envelope(p, length) || !valid(p[2], p[3], p + NRF5_ESB_HEADER_SIZE)) return false;
    *session = 0; *sequence = 0;
    for (uint8_t i = 0; i < 8; i++) *session |= (uint64_t)p[4 + i] << (i * 8);
    for (uint8_t i = 0; i < 4; i++) *sequence |= (uint32_t)p[12 + i] << (i * 8);
    if (!*session) return false;
    report->kind = p[2]; report->length = p[3];
    memcpy(report->data, p + NRF5_ESB_HEADER_SIZE, report->length);
    return true;
}
uint8_t nrf5_esb_encode(uint8_t *p, uint64_t session, uint32_t sequence, const nrf5_esb_report_t *report) {
    if (!session || !valid(report->kind, report->length, report->data)) return 0;
    p[0] = 0x51; p[1] = 2; p[2] = report->kind; p[3] = report->length;
    for (uint8_t i = 0; i < 8; i++) p[4 + i] = session >> (i * 8);
    for (uint8_t i = 0; i < 4; i++) p[12 + i] = sequence >> (i * 8);
    memcpy(p + NRF5_ESB_HEADER_SIZE, report->data, report->length);
    memset(p + NRF5_ESB_HEADER_SIZE + report->length, 0, NRF5_ESB_TAG_SIZE);
    return NRF5_ESB_HEADER_SIZE + report->length + NRF5_ESB_TAG_SIZE;
}
bool nrf5_esb_newer(uint32_t candidate, uint32_t previous) {
    uint32_t distance = candidate - previous;
    return distance && distance < 0x80000000u;
}
