// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_esb.h"
#include "esb_radio.h"
#include "esb_crypto.h"
#include "esb_settings.h"
#include "esb_pair.h"
#ifdef NRF5_ESB_BRIDGE_ENABLE
#    include "esb_bridge.h"
#endif
#include "nrf5_usb.h"
#include "timer.h"
#ifdef NRF5_WIRELESS_POWER_ENABLE
#    include "wireless_power.h"
#endif
#include "nrf_soc.h"
#ifdef RAW_ENABLE
#    include "via_transport.h"
#endif
#include <string.h>

uint32_t nrf5_esb_now(void) {
    // The nRF QMK timer exposes RTC1's 24-bit ticks divided by 32: it wraps
    // every 524288 timer units. Extend it while the ESB task runs so pairing
    // and link deadlines do not jump when that hardware counter rolls over.
    static uint32_t previous, extended;
    uint32_t raw = timer_read32() & 0x7FFFFu;
    extended += (raw - previous) & 0x7FFFFu;
    previous = raw;
    return extended;
}

typedef struct { nrf5_esb_report_t report; uint32_t sequence; } queued_report_t;
static queued_report_t queue[NRF5_ESB_QUEUE_SIZE];
static uint8_t head, count;
static uint32_t next_sequence;
static uint32_t last_activity;
static bool connected;
static bool active;
#ifdef NRF5_ESB_BRIDGE_ENABLE
static bool bridge_pairing;
#endif
#ifdef NRF5_ESB_TX
static uint8_t verified_leds;
#endif
static uint64_t challenge;
static uint32_t configured_address;
static uint8_t configured_key[16];

static void append(uint8_t kind, const void *data, uint8_t length) {
#ifdef NRF5_ESB_TX
    if (next_sequence >= 0xFFFFFF00u) return;
#endif
    queued_report_t *item = &queue[(head + count) % NRF5_ESB_QUEUE_SIZE];
    item->report.kind = kind; item->report.length = length;
    if (length) memcpy(item->report.data, data, length);
    item->sequence = next_sequence++;
    count++;
}
static void release_all(void) {
    uint8_t zero[31] = {0};
    append(NRF5_ESB_KEYBOARD, zero, 8);
    append(NRF5_ESB_NKRO, zero, 31);
    zero[0] = REPORT_ID_SYSTEM; append(NRF5_ESB_EXTRA, zero, 3);
    zero[0] = REPORT_ID_CONSUMER; append(NRF5_ESB_EXTRA, zero, 3);
    zero[0] = 0; append(NRF5_ESB_MOUSE, zero, 5);
}
static uint8_t leds(void) {
#ifdef NRF5_ESB_TX
    return connected ? verified_leds : 0;
#else
    return nrf5_usb_driver()->keyboard_leds();
#endif
}

#ifdef NRF5_ESB_TX
static uint64_t session;
static uint32_t refresh_at, snapshot_at, oldest_at, in_flight;
static bool sending, nkro_mode, hello_sent;
#ifdef NRF5_ESB_BRIDGE_ENABLE
static bool sending_bridge, prefer_bridge, bridge_pending;
static uint8_t bridge_packet[64], bridge_length;
static uint32_t bridge_sequence;
#endif
static bool probing, probe_ready;
static uint8_t probe_attempts;
static uint32_t probe_at;
static uint8_t outgoing[NRF5_ESB_PAYLOAD_MAX];
static uint8_t keyboard[8], nkro[31], mouse_buttons;
static uint8_t system_usage[3] = {REPORT_ID_SYSTEM}, consumer_usage[3] = {REPORT_ID_CONSUMER};

static uint32_t refresh_delay(void) {
#ifdef NRF5_WIRELESS_POWER_ENABLE
    if (nrf5_wireless_idle()) {
#    ifdef NRF5_ESB_BRIDGE_ENABLE
        if (nrf5_bridge_fast_poll()) return NRF5_ESB_REFRESH_MS;
#    endif
        return NRF5_ESB_IDLE_REFRESH_MS;
    }
#endif
    return NRF5_ESB_REFRESH_MS;
}

uint32_t nrf5_esb_next_wake_ticks(void) {
    // Keep the existing short service fallback during transactions, retries,
    // route changes and pairing. Radio IRQs can wake the loop sooner.
    if (!active || sending || count || nrf5_pair_busy() ||
        !nrf5_esb_settings()->paired || nrf5_esb_settings_pending() || nrf5_esb_settings_error())
        return NRF5_ESB_REFRESH_MS;
#ifdef NRF5_ESB_BRIDGE_ENABLE
    if (bridge_pending) return NRF5_ESB_REFRESH_MS;
#endif
    uint32_t now = nrf5_esb_now();
    uint32_t interval = refresh_delay(), elapsed = now - refresh_at;
    uint32_t remaining = elapsed >= interval ? 1 : interval - elapsed;
    if (connected) {
        elapsed = now - last_activity;
        uint32_t supervision = elapsed >= NRF5_ESB_TIMEOUT_MS ? 1 : NRF5_ESB_TIMEOUT_MS - elapsed;
        if (supervision < remaining) remaining = supervision;
    }
    return remaining;
}

static void snapshot(void) {
    append(NRF5_ESB_KEYBOARD, keyboard, sizeof(keyboard));
    append(NRF5_ESB_NKRO, nkro, sizeof(nkro));
    append(NRF5_ESB_EXTRA, system_usage, 3);
    append(NRF5_ESB_EXTRA, consumer_usage, 3);
    const uint8_t mouse[5] = {mouse_buttons};
    append(NRF5_ESB_MOUSE, mouse, 5);
}
static void enqueue(uint8_t kind, const void *data, uint8_t length) {
    if (count == NRF5_ESB_QUEUE_SIZE) {
        // A lost link must never leave a release behind an unbounded backlog.
        // Already submitted radio DMA is immutable; sequence matching below
        // prevents its completion from removing the new recovery head.
        head = count = 0;
        release_all(); snapshot();
    }
    append(kind, data, length);
}
static void send_keyboard(report_keyboard_t *report) {
    if (nkro_mode) {
        memset(nkro, 0, sizeof(nkro));
        enqueue(NRF5_ESB_NKRO, nkro, sizeof(nkro));
    }
    nkro_mode = false;
    memcpy(keyboard, &report->mods, sizeof(keyboard));
    enqueue(NRF5_ESB_KEYBOARD, keyboard, sizeof(keyboard));
}
static void send_nkro(report_nkro_t *report) {
    if (!nkro_mode) {
        memset(keyboard, 0, sizeof(keyboard));
        enqueue(NRF5_ESB_KEYBOARD, keyboard, sizeof(keyboard));
    }
    nkro_mode = true;
    _Static_assert(sizeof(report->bits) == 30, "ESB NKRO wire format changed");
    nkro[0] = report->mods; memcpy(nkro + 1, report->bits, sizeof(report->bits));
    enqueue(NRF5_ESB_NKRO, nkro, sizeof(nkro));
}
static void send_extra(report_extra_t *report) {
    uint8_t *data = report->report_id == REPORT_ID_SYSTEM ? system_usage : consumer_usage;
    data[1] = report->usage; data[2] = report->usage >> 8;
    enqueue(NRF5_ESB_EXTRA, data, 3);
}
static void send_mouse(report_mouse_t *report) {
    const uint8_t data[5] = {report->buttons, report->x, report->y, report->v, report->h};
    mouse_buttons = report->buttons;
    enqueue(NRF5_ESB_MOUSE, data, 5);
}
#else
// The receiver's local QMK events must not overwrite remote USB reports.
static void send_keyboard(report_keyboard_t *report) { (void)report; }
static void send_nkro(report_nkro_t *report) { (void)report; }
static void send_extra(report_extra_t *report) { (void)report; }
static void send_mouse(report_mouse_t *report) { (void)report; }
static uint64_t peer_session;
static uint32_t peer_sequence;
static bool have_peer;

static void receive(uint32_t now) {
    uint8_t packet[NRF5_ESB_PAYLOAD_MAX], length;
    while (nrf5_esb_radio_receive(packet, &length)) {
        if (nrf5_pair_envelope(packet, length)) { nrf5_pair_receive(packet, length); continue; }
        uint8_t encrypted[NRF5_ESB_PAYLOAD_MAX], reply[NRF5_ESB_ACK_SIZE];
        memcpy(encrypted, packet, length);
        uint64_t session = 0; uint32_t sequence;
        nrf5_esb_report_t report;
        const uint8_t *key = nrf5_esb_settings()->key;
        bool valid = nrf5_esb_open(key, challenge, packet, length) && nrf5_esb_decode(packet, length, &session, &sequence, &report);
        bool accepted = false;
        if (valid && report.kind == NRF5_ESB_HELLO) {
            if (session != peer_session || !challenge) {
                uint64_t fresh = 0;
                if (sd_rand_application_vector_get((uint8_t *)&fresh, sizeof(fresh)) != NRF_SUCCESS || !fresh) continue;
                challenge = fresh; peer_session = session; have_peer = false;
#ifdef NRF5_ESB_BRIDGE_ENABLE
                nrf5_bridge_reset();
#endif
                connected = false; head = count = 0; release_all();
            }
            accepted = true;
        } else if (valid && session == peer_session && challenge) {
            accepted = true;
        }
#ifdef NRF5_ESB_BRIDGE_ENABLE
        if (accepted && report.kind == NRF5_ESB_BRIDGE && (!have_peer || nrf5_esb_newer(sequence, peer_sequence)) && !nrf5_bridge_up_ready()) continue;
        if (accepted && report.kind == NRF5_ESB_BRIDGE_ACK) {
            uint32_t down = (uint32_t)report.data[0]<<24 | (uint32_t)report.data[1]<<16 | (uint32_t)report.data[2]<<8 | report.data[3];
            nrf5_bridge_confirm(down);
        } else if (accepted && report.kind == NRF5_ESB_BRIDGE) {
            uint32_t down = (uint32_t)report.data[4]<<24 | (uint32_t)report.data[5]<<16 | (uint32_t)report.data[6]<<8 | report.data[7];
            nrf5_bridge_confirm(down);
        }
#endif
        if (nrf5_esb_ack_seal(key, encrypted, challenge, nrf5_usb_driver()->keyboard_leds(), accepted, reply)) {
#ifdef NRF5_ESB_BRIDGE_ENABLE
            uint8_t extended[64];memcpy(extended,reply,NRF5_ESB_ACK_SIZE);
            if(accepted && report.kind!=NRF5_ESB_HELLO && nrf5_bridge_extension(extended+30,key,session,challenge))
                nrf5_esb_radio_cache_reply(encrypted,length,extended,64);
            else
#endif
            nrf5_esb_radio_cache_ack(encrypted, length, reply);
        }
        if (!accepted || report.kind == NRF5_ESB_HELLO) continue;
        if (!have_peer) {
            head = count = 0; release_all();
            peer_session = session; peer_sequence = sequence - 1; have_peer = true;
        }
        if (sequence == peer_sequence) continue;
        if (!nrf5_esb_newer(sequence, peer_sequence)) continue;
        peer_sequence = sequence; connected = true; last_activity = now;
#ifdef NRF5_ESB_BRIDGE_ENABLE
        if (report.kind == NRF5_ESB_BRIDGE) { nrf5_bridge_up(&report); continue; }
        if (report.kind == NRF5_ESB_BRIDGE_ACK) continue;
#endif
        if (report.kind == NRF5_ESB_HEARTBEAT) continue;
        if (count == NRF5_ESB_QUEUE_SIZE) { head = count = 0; release_all(); }
        append(report.kind, report.data, report.length);
    }
}
#endif
static host_driver_t driver = {
    .keyboard_leds = leds, .send_keyboard = send_keyboard, .send_nkro = send_nkro,
    .send_mouse = send_mouse, .send_extra = send_extra,
#ifdef RAW_ENABLE
    // VIA still addresses the board's own USB interface in both roles.
    .send_raw_hid = nrf_via_send,
#endif
};
static void quiet_keyboard(report_keyboard_t *r) { (void)r; }
static void quiet_nkro(report_nkro_t *r) { (void)r; }
static void quiet_mouse(report_mouse_t *r) { (void)r; }
static void quiet_extra(report_extra_t *r) { (void)r; }
static host_driver_t quiet = {
    .keyboard_leds = leds, .send_keyboard = quiet_keyboard, .send_nkro = quiet_nkro,
    .send_mouse = quiet_mouse, .send_extra = quiet_extra,
#ifdef RAW_ENABLE
    .send_raw_hid = nrf_via_send,
#endif
};
host_driver_t *nrf5_esb_quiet_driver(void) { return &quiet; }
host_driver_t *nrf5_esb_driver(void) { return &driver; }
bool nrf5_esb_connected(void) { return connected; }
void nrf5_esb_reset_link(void) {
    connected=false;challenge=0;head=count=0;
#ifdef NRF5_ESB_TX
    session=0;
#    ifdef NRF5_ESB_BRIDGE_ENABLE
    bridge_pending=false;
#    endif
#else
    have_peer=false;peer_session=0;
#endif
#ifdef NRF5_ESB_BRIDGE_ENABLE
    nrf5_bridge_abort();
#endif
    release_all();
}
__attribute__((weak)) void nrf5_esb_led_diagnostics(uint8_t data[3]) {
    data[0] = leds(); data[1] = data[2] = 0;
}
void nrf5_esb_diagnostics(uint8_t packet[32]) {
    memset(packet + 1, 0, 31); packet[1] = 2;
#ifdef NRF5_ESB_TX
    packet[2] = 1;
#else
    packet[2] = 2;
#endif
    packet[3] = connected;
    uint32_t values[6] = {nrf5_esb_radio_error()};
    nrf5_esb_radio_counters(values + 1);
    for (uint8_t i = 0; i < 6; i++) for (uint8_t b = 0; b < 4; b++) packet[4 + i * 4 + b] = values[i] >> (24 - b * 8);
    packet[28] = count;
    nrf5_esb_led_diagnostics(packet + 29);
}
void nrf5_esb_init(void) {
    nrf5_esb_settings_init();
    nrf5_esb_radio_init();
#ifdef NRF5_ESB_BRIDGE_ENABLE
    nrf5_bridge_reset();
#endif
#ifdef NRF5_ESB_RX
    active = true;
#endif
}
void nrf5_esb_release(void) {
    head = count = 0;
#ifdef NRF5_ESB_TX
    memset(keyboard, 0, sizeof(keyboard)); memset(nkro, 0, sizeof(nkro));
    system_usage[1] = system_usage[2] = consumer_usage[1] = consumer_usage[2] = mouse_buttons = 0;
#endif
    release_all();
}
bool nrf5_esb_idle(void) {
#ifdef NRF5_ESB_TX
    return !count && !sending;
#else
    return true;
#endif
}
void nrf5_esb_set_active(bool value) {
    if (active == value) return;
    active = value;
#ifdef NRF5_ESB_BRIDGE_ENABLE
    if (!active) {
        nrf5_bridge_abort();
#    ifdef NRF5_ESB_TX
        bridge_pending=false;
        session=challenge=0;connected=false;
#    endif
    }
#endif
#ifdef NRF5_ESB_TX
    // Keep the authenticated discovery session when AUTO hands input over to
    // the receiver. Clearing it here would immediately select BLE again.
    if (!probing) { connected = false; challenge = 0; session = 0; }
    probe_ready = false; probe_attempts = 0; probe_at = nrf5_esb_now() - 2000;
#else
    connected = false; challenge = 0;
#endif
    head = count = 0;
#ifdef NRF5_ESB_TX
    if (active) nrf5_esb_release();
#endif
}
#ifdef NRF5_ESB_TX
void nrf5_esb_set_probe(bool enabled) {
    if (probing == enabled) return;
    probing = enabled;
    probe_ready = false; probe_attempts = 0; probe_at = nrf5_esb_now() - 2000;
    if (!active) {
        connected = false; challenge = session = 0;
        head = count = 0;
    }
}
#endif
void nrf5_esb_task(void) {
    const uint32_t now = nrf5_esb_now();
#ifdef NRF5_ESB_BRIDGE_ENABLE
    nrf5_bridge_tick();
#endif
    nrf5_esb_settings_task();
    nrf5_pair_poll_key(now);
#ifdef NRF5_ESB_BRIDGE_ENABLE
    if(nrf5_pair_busy() && !bridge_pairing) {
        // Do not execute a queued edit/note after a long pairing window is
        // cancelled. The next normal connection must authenticate anew.
        bridge_pairing=true;nrf5_esb_reset_link();
    } else if(!nrf5_pair_busy())bridge_pairing=false;
#endif
    if (nrf5_pair_busy()) {
#ifdef NRF5_ESB_TX
        // Drain the old normal-data transaction before the pairer owns the
        // radio result mailbox; never consume its result in both state machines.
        if (sending) { bool ignored; if (!nrf5_esb_radio_result(&ignored)) return; sending = false; }
#endif
        connected = false;
        if (nrf5_pair_task(now)) {
#ifdef NRF5_ESB_RX
            if (count && nrf5_usb_esb_report(queue[head].report.kind, queue[head].report.data, queue[head].report.length)) {
                head = (head + 1) % NRF5_ESB_QUEUE_SIZE; count--;
            }
#endif
            return;
        }
    } else (void)nrf5_pair_task(now); // maintain receiver's lost-DONE grace pipe
    const nrf5_esb_settings_t *settings = nrf5_esb_settings();
    bool enabled = active;
#ifdef NRF5_ESB_TX
    enabled |= probing;
#endif
    bool usable = enabled && settings->paired && !nrf5_esb_settings_pending() && !nrf5_esb_settings_error();
    if (configured_address != settings->address || memcmp(configured_key, settings->key, 16)) {
#ifdef NRF5_ESB_BRIDGE_ENABLE
        nrf5_bridge_reset();
#    ifdef NRF5_ESB_TX
        bridge_pending=false;
#    endif
#endif
        connected = false; challenge = 0;
        configured_address = settings->address; memcpy(configured_key, settings->key, 16);
#ifdef NRF5_ESB_TX
        session = 0; probe_ready = false; probe_attempts = 0; nrf5_esb_release();
#else
        peer_session = 0; have_peer = false; head = count = 0; release_all();
#endif
    }
    nrf5_esb_radio_configure(usable, settings->address);
    nrf5_esb_radio_task();
#ifdef NRF5_ESB_TX
    bool acknowledged;
    if (sending && nrf5_esb_radio_result(&acknowledged)) {
        sending = false;
        uint8_t ack[NRF5_ESB_ACK_SIZE], new_leds; bool accepted;
        uint64_t peer_challenge;
        nrf5_esb_radio_ack(ack);
        bool verified = usable && acknowledged && nrf5_esb_ack_open(settings->key, outgoing, ack, &peer_challenge, &new_leds, &accepted) && accepted &&
            (hello_sent || peer_challenge == challenge);
#ifdef NRF5_ESB_BRIDGE_ENABLE
        // A late completion from the previous output mode must not resurrect
        // a session that a mode change or timeout has already invalidated.
        uint64_t outgoing_session=0;
        for(unsigned i=0;i<8;i++)outgoing_session|=(uint64_t)outgoing[4+i]<<(8*i);
        verified=verified && session && outgoing_session==session;
#endif
        if (verified) {
#ifdef NRF5_ESB_BRIDGE_ENABLE
            if (challenge != peer_challenge) { nrf5_bridge_reset();bridge_pending=false; }
#endif
            challenge = peer_challenge; verified_leds = new_leds;
            oldest_at = last_activity = now; connected = true;
            if (!hello_sent) {
#ifdef NRF5_ESB_BRIDGE_ENABLE
                if (sending_bridge) { nrf5_bridge_sent();prefer_bridge=false;bridge_pending=false; }
                else {
                    prefer_bridge=true;
#endif
                    if (count && queue[head].sequence == in_flight) { head = (head + 1) % NRF5_ESB_QUEUE_SIZE; count--; }
#ifdef NRF5_ESB_BRIDGE_ENABLE
                }
                uint8_t extended[64];
                if(active && nrf5_esb_radio_reply(extended)==64) nrf5_bridge_down(extended+30,settings->key,session,challenge);
#endif
            }
        } else if (usable && acknowledged) challenge = 0;
        if (!active && probing) {
            // Retry the same immutable probe so the receiver can answer from
            // its cache after main-context authentication. Back off after a
            // bounded burst; create a fresh sequence for the next burst.
            if (verified || probe_attempts >= 3) {
                probe_ready = false; probe_attempts = 0; probe_at = now;
            } else probe_at = now; // next retry in 40 timer units
        }
    }
    if (!usable) { connected = false; return; }
    if (!sending && next_sequence >= 0xFFFFFF00u) {
        session = challenge = 0; next_sequence = 0; head = count = 0;
        probe_ready = false; probe_attempts = 0;
        if (active) { release_all(); snapshot(); }
    }
    if (!session) {
        if (sd_rand_application_vector_get((uint8_t *)&session, sizeof(session)) != NRF_SUCCESS || !session) { session = 0; return; }
    }
    if (connected && now - last_activity >= (active ? NRF5_ESB_TIMEOUT_MS : 1000)) {
        connected = false;
#ifdef NRF5_ESB_BRIDGE_ENABLE
        nrf5_bridge_abort();
        bridge_pending=false;
        session=challenge=0;
#endif
    }
    if (!active) {
        // Input belongs to USB/BLE. No snapshots, queued releases or relative
        // mouse movement may escape through background discovery.
        head = count = 0;
        uint32_t probe_interval = 500;
#ifdef NRF5_WIRELESS_POWER_ENABLE
        if (nrf5_wireless_idle()) probe_interval = 2000;
#endif
        // Keep the short authentication retry burst even in idle mode.
        if (probe_ready) probe_interval = 40;
        if (sending || now - probe_at < probe_interval) return;
        if (!probe_ready) {
            nrf5_esb_report_t hello = {.kind = NRF5_ESB_HELLO};
            uint8_t length = nrf5_esb_encode(outgoing, session, next_sequence++, &hello);
            if (!nrf5_esb_seal(settings->key, 0, outgoing, length)) return;
            probe_ready = true;
        }
        if (nrf5_esb_radio_send(outgoing, NRF5_ESB_HEADER_SIZE + NRF5_ESB_TAG_SIZE)) {
            sending = hello_sent = true; probe_attempts++; probe_at = now;
        }
        return;
    }
    if (!sending && count && now - oldest_at >= NRF5_ESB_TIMEOUT_MS) {
        head = count = 0; release_all(); snapshot(); oldest_at = now;
    }
    uint32_t refresh_interval = refresh_delay();
    uint32_t snapshot_interval = NRF5_ESB_SNAPSHOT_MS;
#ifdef NRF5_WIRELESS_POWER_ENABLE
    if (nrf5_wireless_idle()) {
        snapshot_interval = NRF5_ESB_IDLE_SNAPSHOT_MS;
#    ifdef NRF5_ESB_BRIDGE_ENABLE
        if (nrf5_bridge_fast_poll()) {
            snapshot_interval = NRF5_ESB_SNAPSHOT_MS;
        }
#    endif
    }
#endif
    if (!count && now - refresh_at >= refresh_interval) {
        // One authenticated heartbeat suffices to keep the link alive and
        // collect LED/control downlinks. Refresh all five HID states less
        // often; changed input still enters the reliable queue immediately.
        if (now - snapshot_at >= snapshot_interval) {
            snapshot(); snapshot_at = now;
        } else {
            append(NRF5_ESB_HEARTBEAT, NULL, 0);
        }
        refresh_at = now;
    }
#ifdef NRF5_ESB_BRIDGE_ENABLE
    nrf5_esb_report_t service;
    bool bridge = !sending && challenge && connected && (bridge_pending || ((prefer_bridge || !count) && nrf5_bridge_next(&service)));
#else
    bool bridge = false;
#endif
    if (!sending && (count || bridge)) {
        // Allocate a fresh counter for each HELLO. Data counters belong to
        // immutable queue entries; retries produce identical CCM ciphertext.
        hello_sent = !challenge;
        nrf5_esb_report_t hello = {.kind = NRF5_ESB_HELLO};
        uint32_t sequence;
        if(hello_sent)sequence=next_sequence++;
#ifdef NRF5_ESB_BRIDGE_ENABLE
        else if(bridge) {
            if(!bridge_pending) {
                bridge_sequence=next_sequence++;
                // Only unsent HID entries may follow a service packet. Move
                // their counters forward to preserve the global replay order.
                for(unsigned i=0;i<count;i++)queue[(head+i)%NRF5_ESB_QUEUE_SIZE].sequence=next_sequence++;
                bridge_length=nrf5_esb_encode(bridge_packet,session,bridge_sequence,&service);
                if(!nrf5_esb_seal(settings->key,challenge,bridge_packet,bridge_length))return;
                bridge_pending=true;
            }
            sequence=bridge_sequence;
        }
#endif
        else sequence=queue[head].sequence;
        const nrf5_esb_report_t *selected = hello_sent ? &hello : &queue[head].report;
#ifdef NRF5_ESB_BRIDGE_ENABLE
        if(bridge && !hello_sent)selected=&service;
        if(!bridge && !hello_sent)prefer_bridge=false;
#endif
        uint8_t length;
        bool sealed;
#ifdef NRF5_ESB_BRIDGE_ENABLE
        if(bridge && !hello_sent) { length=bridge_length;memcpy(outgoing,bridge_packet,length);sealed=true; }
        else
#endif
        { length=nrf5_esb_encode(outgoing, session, sequence, selected);sealed=nrf5_esb_seal(settings->key, challenge, outgoing, length); }
        if (sealed && nrf5_esb_radio_send(outgoing, length)) {
            sending = true; in_flight = sequence;
#ifdef NRF5_ESB_BRIDGE_ENABLE
            sending_bridge = bridge && !hello_sent;
#endif
            if (count == 1) oldest_at = now;
        }
    }
#else
    if (usable) receive(now);
    else if (connected) { connected = have_peer = false; head = count = 0; release_all(); }
    if (connected && now - last_activity >= NRF5_ESB_TIMEOUT_MS) {
        connected = false;
#ifdef NRF5_ESB_BRIDGE_ENABLE
        challenge=0;have_peer=false;
        nrf5_bridge_abort();
#endif
        head = count = 0; release_all();
    }
    // Only main context touches USB. Keep the head on BUSY; its copy stays
    // in the USB driver's dedicated DMA buffer until IN_REPORT_DONE.
    if (count && nrf5_usb_esb_report(queue[head].report.kind, queue[head].report.data, queue[head].report.length)) {
        head = (head + 1) % NRF5_ESB_QUEUE_SIZE; count--;
    }
#endif
}
