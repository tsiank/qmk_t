// SPDX-License-Identifier: GPL-2.0-or-later
// Physical-window X25519 "Just Works" pairing. This does
// not authenticate a previously unknown peer against an active MITM.
#include "esb_pair.h"
#include "esb_radio.h"
#include "esb_settings.h"
#include "nrf5_esb.h"
#include "nrf_soc.h"
#include "timer.h"
#include <string.h>
#ifdef NRF5_ESB_PAIR_COMBO
#    include "matrix.h"
static const uint8_t pair_combo[][2] = NRF5_ESB_PAIR_COMBO;
static bool combo_down, combo_fired;
static uint32_t combo_at;
#endif

enum { OFFER = 8, WAIT = 10, DONE = 11 };
static uint8_t phase;
static bool running, ending, waiting, private_ready, keys_ready, derived, approved, have_finish;
static uint32_t started, saved_at, last_send, grace_at;
#ifdef NRF5_ESB_TX
static uint32_t peer_seen_at;
#endif
static bool grace;
#ifdef NRF5_ESB_DONGLE
static bool usb_window;
static uint32_t usb_window_at;
#endif
static uint64_t local_nonce, peer_nonce;
static uint8_t private_key[32], public_key[32], peer_public[32], link_key[16], confirm_key[16];
static uint32_t pair_address, fingerprint;
static uint8_t sent[64];
#ifdef NRF5_ESB_TX
static uint8_t sent_length;
#endif
#ifdef NRF5_ESB_RX
static uint8_t finish[24];
#endif
static bool key_down, key_fired;
static uint32_t key_at;

static uint64_t read64(const uint8_t *p) {
    uint64_t v = 0; for (uint8_t i = 0; i < 8; i++) v |= (uint64_t)p[i] << (i * 8); return v;
}
static void wipe(void) {
    memset(private_key, 0, sizeof(private_key)); memset(link_key, 0, sizeof(link_key));
    memset(confirm_key, 0, sizeof(confirm_key)); memset(sent, 0, sizeof(sent));
    memset(peer_public, 0, sizeof(peer_public));
    private_ready = keys_ready = derived = approved = have_finish = false;
    local_nonce = peer_nonce = 0;
}
static void stop(uint8_t state) {
    phase = state; running = false; ending = true; grace = false;
}
bool nrf5_pair_start(void) {
    if (running || ending || nrf5_esb_settings_pending()) return false;
    wipe(); grace = false; fingerprint = 0; pair_address = 0;
    running = true; started = nrf5_esb_now(); phase = NRF5_PAIR_WINDOW; last_send = started - 50;
    nrf5_esb_release();
#ifdef NRF5_ESB_DONGLE
    usb_window = false;
#endif
    return true;
}
#ifdef NRF5_ESB_DONGLE
void nrf5_pair_usb_window(void) {
    usb_window_at = nrf5_esb_now(); usb_window = true;
    // An unbound dongle has no private radio address to listen on.
    if (!nrf5_esb_settings()->paired) (void)nrf5_pair_start();
}
#endif
bool nrf5_pair_confirm(void) {
#ifdef NRF5_ESB_RX
    if (running && phase == NRF5_PAIR_CONFIRM && derived && have_finish && nrf5_esb_now() - started < NRF5_ESB_PAIR_WINDOW_MS) { approved = true; return true; }
#endif
    return false;
}
bool nrf5_pair_cancel(void) {
#ifdef NRF5_ESB_DONGLE
    if (usb_window && !running) { usb_window = false; phase = NRF5_PAIR_CANCELLED; return true; }
#endif
    if (!running || phase == NRF5_PAIR_SAVING) return false;
    stop(NRF5_PAIR_CANCELLED); return true;
}
bool nrf5_pair_busy(void) { return running || ending; }
uint8_t nrf5_pair_state(void) {
#ifdef NRF5_ESB_DONGLE
    if (usb_window && !running) return NRF5_PAIR_WINDOW;
#endif
    return phase;
}
uint32_t nrf5_pair_fingerprint(void) { return fingerprint; }
uint8_t nrf5_pair_seconds(void) {
#ifdef NRF5_ESB_DONGLE
    if (usb_window && !running) {
        uint32_t elapsed = nrf5_esb_now() - usb_window_at;
        return elapsed < NRF5_ESB_PAIR_WINDOW_MS ? (NRF5_ESB_PAIR_WINDOW_MS - elapsed + 999) / 1000 : 0;
    }
#endif
    uint32_t elapsed = nrf5_esb_now() - started;
    return running && elapsed < NRF5_ESB_PAIR_WINDOW_MS ? (NRF5_ESB_PAIR_WINDOW_MS - elapsed + 999) / 1000 : 0;
}
void nrf5_pair_key(bool pressed) {
    if (pressed) { key_down = true; key_fired = false; key_at = nrf5_esb_now(); }
    else {
#ifdef NRF5_ESB_RX
        if (key_down && !key_fired) (void)nrf5_pair_confirm();
#endif
        key_down = false;
    }
}
void nrf5_pair_poll_key(uint32_t now) {
    if (key_down && !key_fired && now - key_at >= 3000) { key_fired = true; (void)nrf5_pair_start(); }
#ifdef NRF5_ESB_PAIR_COMBO
    bool pressed = true;
    for (uint8_t i = 0; i < sizeof(pair_combo) / sizeof(pair_combo[0]); i++) pressed &= matrix_is_on(pair_combo[i][0], pair_combo[i][1]);
    if (pressed && !combo_down) { combo_at = now; combo_fired = false; }
    combo_down = pressed;
    if (pressed && !combo_fired && now - combo_at >= 3000) { combo_fired = true; (void)nrf5_pair_start(); }
#endif
}
#ifdef NRF5_ESB_TX
static void header(uint8_t *p, uint8_t type, uint8_t body, uint64_t nonce, uint8_t sequence) {
    memset(p, 0, 64); p[0] = 0x51; p[1] = 3; p[2] = type; p[3] = body;
    for (uint8_t i = 0; i < 8; i++) p[4 + i] = nonce >> (i * 8);
    p[12] = sequence;
}
#endif
static bool generate(void) {
    if (!private_ready) {
        if (sd_rand_application_vector_get(private_key, 32) != NRF_SUCCESS) return false;
        private_ready = true;
    }
    if (!local_nonce && (sd_rand_application_vector_get((uint8_t *)&local_nonce, 8) != NRF_SUCCESS || !local_nonce)) { local_nonce = 0; return false; }
    if (!keys_ready) { nrf5_pair_public(public_key, private_key); keys_ready = true; }
    return true;
}

#ifdef NRF5_ESB_RX
static void reply_finish(void) {
    uint8_t reply[64];
    memcpy(reply, finish, 16); reply[0] = 0xA3; reply[2] = phase == NRF5_PAIR_COMPLETE ? DONE : WAIT;
    nrf5_pair_sign(confirm_key, reply, 24);
    nrf5_esb_radio_cache_reply(finish, 24, reply, 24);
}
#endif
void nrf5_pair_receive(const uint8_t *p, uint8_t length) {
#ifdef NRF5_ESB_RX
#    ifdef NRF5_ESB_DONGLE
    // A bound receiver listens on its private pipe and the discovery pipe at
    // once. Only a keyboard's explicit PUBLIC request interrupts the old link.
    if (!running && usb_window && nrf5_esb_now() - usb_window_at < NRF5_ESB_PAIR_WINDOW_MS &&
        nrf5_pair_envelope(p, length) && p[2] == NRF5_PAIR_PUBLIC && p[12] == 1 &&
        !p[13] && !p[14] && !p[15] && read64(p + 4)) {
        for (uint8_t i = 48; i < 56; i++) if (p[i]) return;
        (void)nrf5_pair_start();
        // Key generation is performed in the next main-loop iteration. The
        // transmitter retries PUBLIC until it receives a verified offer.
    }
#    endif
    if ((!running && !grace) || !keys_ready || !nrf5_pair_envelope(p, length)) return;
    uint64_t nonce = read64(p + 4);
    if (!nonce || p[13] || p[14] || p[15]) return;
    if (p[2] == NRF5_PAIR_PUBLIC && p[12] == 1 && running && phase != NRF5_PAIR_SAVING) {
        for (uint8_t i = 48; i < 56; i++) if (p[i]) return;
        if (derived && (nonce != peer_nonce || memcmp(p + 16, peer_public, 32))) return;
        if (!derived) {
            memcpy(peer_public, p + 16, 32);
            if (!nrf5_pair_derive(private_key, peer_public, peer_public, public_key, nonce, local_nonce,
                                 link_key, confirm_key, &pair_address, &fingerprint)) return;
            peer_nonce = nonce; derived = true; phase = NRF5_PAIR_CANDIDATE;
        }
        uint8_t reply[64]; memcpy(reply, p, 16); reply[0] = 0xA3; reply[2] = OFFER; reply[3] = 40;
        memcpy(reply + 16, public_key, 32);
        for (uint8_t i = 0; i < 8; i++) reply[48 + i] = local_nonce >> (i * 8);
        nrf5_pair_sign(confirm_key, reply, 64);
        nrf5_esb_radio_cache_reply(p, length, reply, 64);
    } else if (p[2] == NRF5_PAIR_FINISH && p[12] == 2 && derived && nonce == peer_nonce && nrf5_pair_verify(confirm_key, p, length)) {
        memcpy(finish, p, 24); have_finish = true;
        if (phase == NRF5_PAIR_CANDIDATE) phase = NRF5_PAIR_CONFIRM;
        reply_finish();
    }
#else
    (void)p; (void)length;
#endif
}
bool nrf5_pair_task(uint32_t now) {
#ifdef NRF5_ESB_DONGLE
    if (usb_window && now - usb_window_at >= NRF5_ESB_PAIR_WINDOW_MS) usb_window = false;
#endif
    if (ending) {
        nrf5_esb_radio_configure(false, NRF5_ESB_PAIR_ADDRESS);
        if (waiting) { bool ignored; if (!nrf5_esb_radio_result(&ignored)) return true; waiting = false; }
        wipe(); ending = false; nrf5_esb_radio_listen_pair(false); return false;
    }
    if (!running) {
        if (grace && now - grace_at >= 20000) { grace = false; wipe(); }
        bool discover = grace;
#ifdef NRF5_ESB_DONGLE
        discover |= usb_window;
#endif
        nrf5_esb_radio_listen_pair(discover); return false;
    }
    bool expired = now - started >= NRF5_ESB_PAIR_WINDOW_MS;
#ifdef NRF5_ESB_TX
    // After key exchange, allow the receiver's window / bounded Flash save to
    // finish even if its clock started later. Remain locked to this candidate.
    if (derived) expired = now - peer_seen_at >= 35000 || now - started >= 150000;
#endif
    if (phase != NRF5_PAIR_SAVING && !approved && expired) { stop(NRF5_PAIR_TIMEOUT); return true; }
    if (phase == NRF5_PAIR_SAVING) {
        if (!nrf5_esb_settings_pending() && !nrf5_esb_settings_error()) {
            const nrf5_esb_settings_t *settings = nrf5_esb_settings();
            if (!settings->paired || settings->address != pair_address || memcmp(settings->key, link_key, 16)) { stop(NRF5_PAIR_FAILED); return true; }
            phase = NRF5_PAIR_COMPLETE; running = false;
#ifdef NRF5_ESB_RX
            grace = true; grace_at = now; reply_finish();
            nrf5_esb_radio_listen_pair(true);
#else
            wipe(); nrf5_esb_radio_listen_pair(false);
#endif
            return false;
        }
        if (now - saved_at >= 30000) { stop(NRF5_PAIR_FAILED); return true; }
    }
    nrf5_esb_radio_listen_pair(true);
    nrf5_esb_radio_configure(true, NRF5_ESB_PAIR_ADDRESS);
    nrf5_esb_radio_task();
    if (!generate()) return true;
#ifdef NRF5_ESB_TX
    if (waiting) {
        bool acknowledged;
        if (!nrf5_esb_radio_result(&acknowledged)) return true;
        waiting = false;
        uint8_t reply[64]; uint8_t size = nrf5_esb_radio_reply(reply);
        if (acknowledged && reply[0] == 0xA3 && reply[1] == 3 && !memcmp(reply + 4, sent + 4, 12)) {
            if (sent[2] == NRF5_PAIR_PUBLIC && size == 64 && reply[2] == OFFER && reply[3] == 40) {
                uint64_t nonce = read64(reply + 48);
                if (nonce && nrf5_pair_derive(private_key, reply + 16, public_key, reply + 16, local_nonce, nonce,
                                             link_key, confirm_key, &pair_address, &fingerprint) && nrf5_pair_verify(confirm_key, reply, size)) {
                    peer_nonce = nonce; peer_seen_at = now; derived = true; phase = NRF5_PAIR_CANDIDATE;
                }
            } else if (sent[2] == NRF5_PAIR_FINISH && size == 24 && derived && nrf5_pair_verify(confirm_key, reply, size)) {
                if (reply[2] == WAIT || reply[2] == DONE) peer_seen_at = now;
                if (reply[2] == DONE) {
                    // Pairing must not turn an explicitly selected AUTO mode
                    // into fixed RF, otherwise later unplugging the receiver
                    // silently disables automatic Bluetooth fallback again.
                    uint8_t mode = nrf5_esb_settings()->mode == NRF5_MODE_AUTO ? NRF5_MODE_AUTO : NRF5_MODE_ESB;
                    if (!nrf5_esb_settings_pair(link_key, pair_address) || !nrf5_esb_settings_mode(mode)) { stop(NRF5_PAIR_FAILED); return true; }
                    phase = NRF5_PAIR_SAVING; saved_at = now; return true;
                }
            }
        }
    }
    if (phase != NRF5_PAIR_SAVING && now - last_send >= 30) {
        header(sent, derived ? NRF5_PAIR_FINISH : NRF5_PAIR_PUBLIC, derived ? 0 : 32, local_nonce, derived ? 2 : 1);
        sent_length = derived ? 24 : 56;
        if (derived) nrf5_pair_sign(confirm_key, sent, sent_length);
        else memcpy(sent + 16, public_key, 32);
        if (nrf5_esb_radio_send(sent, sent_length)) { waiting = true; last_send = now; }
    }
#else
    uint8_t packet[64], length;
    while (nrf5_esb_radio_receive(packet, &length)) nrf5_pair_receive(packet, length);
#ifdef NRF5_ESB_DONGLE
    // USB insertion (or an optional local management command) authorizes the
    // window. A valid FINISH proves both peers derived the same fresh key;
    // no PC confirmation, receiver button or display is required.
    if (phase == NRF5_PAIR_CONFIRM && !expired) approved = true;
#endif
    if (phase == NRF5_PAIR_CONFIRM && approved) {
        if (!nrf5_esb_settings_pair(link_key, pair_address)) { stop(NRF5_PAIR_FAILED); return true; }
        phase = NRF5_PAIR_SAVING; saved_at = now;
        reply_finish();
    }
#endif
    return true;
}
