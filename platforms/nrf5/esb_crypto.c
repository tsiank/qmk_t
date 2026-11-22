// SPDX-License-Identifier: GPL-2.0-or-later
// RFC 3610 CCM (13-byte nonce, 8-byte tag), RFC 4493 CMAC for ACKs.
#include "esb_crypto.h"
#include "nrf_soc.h"
#include <string.h>

static bool aes(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    nrf_ecb_hal_data_t block __attribute__((aligned(4)));
    memcpy(block.key, key, 16); memcpy(block.cleartext, in, 16);
    bool success = sd_ecb_block_encrypt(&block) == NRF_SUCCESS;
    if (success) memcpy(out, block.ciphertext, 16);
    memset(&block, 0, sizeof(block));
    return success;
}
static bool data_key(const uint8_t master[16], const uint8_t *p, uint64_t challenge, uint8_t key[16]) {
    static const uint8_t hello[16] = "Qmknrf5 HELLOv2", data[16] = "Qmknrf5 DATA v2", down[16] = "Qmknrf5 DOWN v1";
    if (p[2] == NRF5_ESB_HELLO) return aes(master, hello, key);
    uint8_t root[16], context[16]; memcpy(context, p + 4, 8);
    for (uint8_t i = 0; i < 8; i++) context[8 + i] = challenge >> (i * 8);
    bool reverse = p[2] >= NRF5_ESB_DOWN_VIA && p[2] <= NRF5_ESB_DOWN_MIDI;
    return aes(master, reverse ? down : data, root) && aes(root, context, key);
}
static bool equal(const uint8_t *a, const uint8_t *b, uint8_t size) {
    uint8_t diff = 0;
    for (uint8_t i = 0; i < size; i++) diff |= a[i] ^ b[i];
    return !diff;
}
static bool mac_block(const uint8_t key[16], uint8_t state[16], const uint8_t block[16]) {
    for (uint8_t i = 0; i < 16; i++) state[i] ^= block[i];
    return aes(key, state, state);
}
static void nonce(const uint8_t *p, uint64_t challenge, uint8_t out[13]) {
    bool hello = p[2] == NRF5_ESB_HELLO;
    for (uint8_t i = 0; i < 8; i++) out[i] = p[4 + i] ^ (hello ? 0 : (uint8_t)(challenge >> (i * 8)));
    memcpy(out + 8, p + 12, 4);
    out[12] = hello ? 2 : p[2] >= NRF5_ESB_DOWN_VIA && p[2] <= NRF5_ESB_DOWN_MIDI ? 1 : 0;
}
static bool ccm_mac(const uint8_t key[16], const uint8_t n[13], const uint8_t *p, uint8_t tag[16]) {
    uint8_t b[16] = {0x59}; // Adata=1, M=8, L=2
    memcpy(b + 1, n, 13); b[15] = p[3];
    memset(tag, 0, 16);
    if (!mac_block(key, tag, b)) return false;
    // Authenticate the complete 16-byte public header, prefixed by its length.
    memset(b, 0, 16); b[1] = NRF5_ESB_HEADER_SIZE; memcpy(b + 2, p, 14);
    if (!mac_block(key, tag, b)) return false;
    memset(b, 0, 16); memcpy(b, p + 14, 2);
    if (!mac_block(key, tag, b)) return false;
    for (uint8_t pos = 0; pos < p[3]; pos += 16) {
        uint8_t size = p[3] - pos; if (size > 16) size = 16;
        memset(b, 0, 16); memcpy(b, p + NRF5_ESB_HEADER_SIZE + pos, size);
        if (!mac_block(key, tag, b)) return false;
    }
    return true;
}
static bool ctr(const uint8_t key[16], const uint8_t n[13], uint8_t *p, uint8_t s0[16]) {
    uint8_t b[16] = {1}, stream[16]; memcpy(b + 1, n, 13);
    if (!aes(key, b, s0)) return false;
    for (uint8_t pos = 0, counter = 1; pos < p[3]; pos += 16, counter++) {
        b[15] = counter;
        if (!aes(key, b, stream)) return false;
        uint8_t size = p[3] - pos; if (size > 16) size = 16;
        for (uint8_t i = 0; i < size; i++) p[NRF5_ESB_HEADER_SIZE + pos + i] ^= stream[i];
    }
    return true;
}
bool nrf5_esb_seal(const uint8_t key[16], uint64_t challenge, uint8_t *p, uint8_t size) {
    if (!nrf5_esb_envelope(p, size)) return false;
    uint8_t n[13], tag[16], s0[16], derived[16]; nonce(p, challenge, n);
    if (!data_key(key, p, challenge, derived) || !ccm_mac(derived, n, p, tag) || !ctr(derived, n, p, s0)) return false;
    for (uint8_t i = 0; i < 8; i++) p[size - 8 + i] = tag[i] ^ s0[i];
    return true;
}
bool nrf5_esb_open(const uint8_t key[16], uint64_t challenge, uint8_t *p, uint8_t size) {
    if (!nrf5_esb_envelope(p, size)) return false;
    uint8_t n[13], tag[16], s0[16], expected[8], derived[16]; nonce(p, challenge, n);
    if (!data_key(key, p, challenge, derived) || !ctr(derived, n, p, s0) || !ccm_mac(derived, n, p, tag)) return false;
    for (uint8_t i = 0; i < 8; i++) expected[i] = tag[i] ^ s0[i];
    if (equal(expected, p + size - 8, 8)) return true;
    memset(p + NRF5_ESB_HEADER_SIZE, 0, p[3]); return false;
}
static void double_block(uint8_t b[16]) {
    uint8_t high = b[0] >> 7;
    for (uint8_t i = 0; i < 15; i++) b[i] = (b[i] << 1) | (b[i + 1] >> 7);
    b[15] <<= 1; if (high) b[15] ^= 0x87;
}
static bool ack_mac(const uint8_t key[16], const uint8_t ack[22], uint8_t out[16]) {
    // Domain-separated, fixed 38-byte message; the last block is partial.
    uint8_t k2[16] = {0}, state[16] = {0}, last[16] = {0}, derived[16];
    static const uint8_t domain[16] = "Qmknrf5 ACK v2";
    if (!aes(key, domain, derived) || !aes(derived, k2, k2)) return false;
    double_block(k2); double_block(k2);
    if (!mac_block(derived, state, domain) || !mac_block(derived, state, ack)) return false;
    memcpy(last, ack + 16, 6); last[6] = 0x80;
    for (uint8_t i = 0; i < 16; i++) last[i] ^= k2[i];
    if (!mac_block(derived, state, last)) return false;
    memcpy(out, state, 16); return true;
}
bool nrf5_esb_ack_seal(const uint8_t key[16], const uint8_t *p, uint64_t challenge, uint8_t leds, bool accepted, uint8_t ack[NRF5_ESB_ACK_SIZE]) {
    uint8_t tag[16]; ack[0] = 0xA2; memcpy(ack + 1, p + 4, 12);
    ack[13] = (leds & 31) | (accepted ? 0x80 : 0);
    for (uint8_t i = 0; i < 8; i++) ack[14 + i] = challenge >> (i * 8);
    if (!ack_mac(key, ack, tag)) return false;
    memcpy(ack + 22, tag, 8); return true;
}
bool nrf5_esb_ack_open(const uint8_t key[16], const uint8_t *p, const uint8_t ack[NRF5_ESB_ACK_SIZE], uint64_t *challenge, uint8_t *leds, bool *accepted) {
    uint8_t tag[16];
    if (ack[0] != 0xA2 || !equal(ack + 1, p + 4, 12) || !ack_mac(key, ack, tag) || !equal(tag, ack + 22, 8)) return false;
    *challenge = 0; for (uint8_t i = 0; i < 8; i++) *challenge |= (uint64_t)ack[14 + i] << (i * 8);
    *leds = ack[13] & 31; *accepted = !!(ack[13] & 0x80);
    return true;
}
