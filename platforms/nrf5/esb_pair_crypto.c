// SPDX-License-Identifier: GPL-2.0-or-later
#include "esb_pair.h"
#include "occ_curve25519.h"
#include "occ_hkdf_sha256.h"
#include "occ_hmac_sha256.h"
#include <string.h>

void nrf5_pair_public(uint8_t public_key[32], uint8_t private_key[32]) {
    private_key[0] &= 248; private_key[31] &= 127; private_key[31] |= 64;
    occ_curve25519_scalarmult_base(public_key, private_key);
}
bool nrf5_pair_derive(const uint8_t private_key[32], const uint8_t peer_public[32],
    const uint8_t tx_public[32], const uint8_t rx_public[32], uint64_t tx_nonce, uint64_t rx_nonce,
    uint8_t link_key[16], uint8_t confirm_key[16], uint32_t *address, uint32_t *fingerprint) {
    uint8_t shared[32], material[32], salt[16], info[88] = "Qmknrf5 ESB pairing v3", digest[32];
    occ_curve25519_scalarmult(shared, private_key, peer_public);
    uint8_t any = 0; for (uint8_t i = 0; i < 32; i++) any |= shared[i];
    if (!any) return false; // RFC 7748: reject low-order/all-zero shared secrets.
    for (uint8_t i = 0; i < 8; i++) { salt[i] = tx_nonce >> (i * 8); salt[8 + i] = rx_nonce >> (i * 8); }
    memcpy(info + 24, tx_public, 32); memcpy(info + 56, rx_public, 32);
    occ_hkdf_sha256(material, 32, shared, 32, salt, 16, info, sizeof(info));
    memcpy(link_key, material, 16); memcpy(confirm_key, material + 16, 16);
    static const uint8_t label[] = "Qmknrf5 address v3";
    occ_hmac_sha256(digest, confirm_key, 16, label, sizeof(label) - 1);
    *address = ((uint32_t)digest[0] << 24 | (uint32_t)digest[1] << 16 | (uint32_t)digest[2] << 8 | digest[3]) | 0x01010101u;
    *fingerprint = (uint32_t)digest[4] << 16 | (uint32_t)digest[5] << 8 | digest[6];
    memset(shared, 0, sizeof(shared)); memset(material, 0, sizeof(material));
    return true;
}
void nrf5_pair_sign(const uint8_t key[16], uint8_t *packet, uint8_t length) {
    uint8_t digest[32]; occ_hmac_sha256(digest, key, 16, packet, length - 8);
    memcpy(packet + length - 8, digest, 8);
}
bool nrf5_pair_verify(const uint8_t key[16], const uint8_t *packet, uint8_t length) {
    if (length < 24 || length > 64) return false;
    uint8_t digest[32], diff = 0; occ_hmac_sha256(digest, key, 16, packet, length - 8);
    for (uint8_t i = 0; i < 8; i++) diff |= digest[i] ^ packet[length - 8 + i];
    return !diff;
}
