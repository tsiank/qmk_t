// SPDX-License-Identifier: GPL-2.0-or-later
// ESB dynamic-payload framing, ACK and retries within S140 radio timeslots.
// The SDK's standalone nrf_esb.c owns RADIO/TIMER/SWI IRQs; it must not be
// linked alongside this driver. Only RADIO and TIMER0 are touched, and only
// while the SoftDevice has granted their use to our priority-0 callback.
#include "esb_radio.h"
#include "esb_pair.h"
#include "nrf.h"
#include "nrf_soc.h"
#include "nrf_sdh_soc.h"
#include "app_error.h"
#include <string.h>

#ifndef NRF5_ESB_CHANNEL
#    define NRF5_ESB_CHANNEL 40
#endif
#ifndef NRF5_ESB_ADDRESS
#    define NRF5_ESB_ADDRESS 0xC3A596E7u
#endif
#if NRF5_ESB_CHANNEL > 100 || NRF5_ESB_CHANNEL < 0
#    error Invalid NRF5_ESB_CHANNEL
#endif

#define SLOT_US 8000u
#define STOP_US (SLOT_US - 200u)
#define ACK_WAIT_US 700u
#define IRQ_TIMER_END TIMER_INTENSET_COMPARE0_Msk
#define IRQ_TIMER_ACK TIMER_INTENSET_COMPARE1_Msk

typedef struct { uint8_t length, s1, data[NRF5_ESB_PAYLOAD_MAX]; } rf_packet_t;
static rf_packet_t rx __attribute__((aligned(4))), tx __attribute__((aligned(4))), ack __attribute__((aligned(4)));
static rf_packet_t received[NRF5_ESB_QUEUE_SIZE];
static volatile uint8_t rx_head, rx_tail;
static volatile bool session_idle, result_ready, tx_pending, tx_acked;
static volatile uint32_t radio_error;
static volatile uint32_t counters[5];
// ACK cache hits, completed ACK TX, CRC errors, rejected ACK, FIFO enqueues,
// main-context ACK cache writes. Keep the existing 0x83 counters unchanged.
static volatile uint32_t details[6];
static volatile uint8_t last_rx_length, last_rx_s1, last_rx_match, last_ack_reject;
static volatile bool enabled;
static volatile uint32_t address;
typedef struct { uint8_t length, reply_length, packet[NRF5_ESB_PAYLOAD_MAX], reply[64]; } ack_cache_t;
static ack_cache_t cached[2];
static volatile uint8_t cache_index;
static volatile bool cache_ready;
static uint8_t last_ack[64], last_ack_length;
static volatile bool pair_listening;
enum { RF_LISTEN, RF_SEND_DATA, RF_WAIT_ACK, RF_SEND_ACK, RF_IDLE };
static uint8_t state;
static nrf_radio_request_t request = {
    .request_type = NRF_RADIO_REQ_TYPE_EARLIEST,
    .params.earliest = {.hfclk = NRF_RADIO_HFCLK_CFG_XTAL_GUARANTEED,
        .priority = NRF_RADIO_PRIORITY_NORMAL, .length_us = SLOT_US, .timeout_us = 1000000},
};
static nrf_radio_signal_callback_return_param_t action;

static uint32_t now_us(void) {
    NRF_TIMER0->TASKS_CAPTURE[2] = 1;
    return NRF_TIMER0->CC[2];
}
static void listen(uint8_t next_state) {
    state = next_state;
    NRF_RADIO->PACKETPTR = (uint32_t)(uintptr_t)&rx;
    NRF_RADIO->EVENTS_END = NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_RXEN = 1;
}
static void transmit(rf_packet_t *packet, uint8_t next_state) {
    state = next_state;
    // Start ACK reception in hardware at TX completion. A software RXEN from
    // the SoftDevice callback can arrive after the peer's ACK preamble.
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |
        (next_state == RF_SEND_DATA ? RADIO_SHORTS_DISABLED_RXEN_Msk : 0);
    if (next_state == RF_SEND_ACK) NRF_RADIO->TXADDRESS = NRF_RADIO->RXMATCH;
    NRF_RADIO->PACKETPTR = (uint32_t)(uintptr_t)packet;
    NRF_RADIO->EVENTS_END = NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_TXEN = 1;
    if (next_state == RF_SEND_DATA) counters[1]++;
}
static void setup_radio(void) {
    NRF_RADIO->POWER = 0; NRF_RADIO->POWER = 1;
    NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_2Mbit;
    NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_0dBm;
    NRF_RADIO->FREQUENCY = NRF5_ESB_CHANNEL;
    // ESB extended DPL: length byte + 3-bit PID/ACK field, 5-byte address.
    NRF_RADIO->PCNF0 = (8u << RADIO_PCNF0_LFLEN_Pos) | (3u << RADIO_PCNF0_S1LEN_Pos);
    NRF_RADIO->PCNF1 = (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
        (4u << RADIO_PCNF1_BALEN_Pos) | NRF5_ESB_PAYLOAD_MAX;
    NRF_RADIO->BASE0 = address;
    NRF_RADIO->BASE1 = NRF5_ESB_PAIR_ADDRESS;
    NRF_RADIO->PREFIX0 = 0xA7A7;
    NRF_RADIO->TXADDRESS = 0;
    NRF_RADIO->RXADDRESSES = pair_listening && address != NRF5_ESB_PAIR_ADDRESS ? 3 : 1;
    NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Two;
    NRF_RADIO->CRCINIT = 0xFFFF; NRF_RADIO->CRCPOLY = 0x11021;
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
    NVIC_EnableIRQ(RADIO_IRQn);
}
static void shutdown_radio(void) {
    NRF_TIMER0->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->SHORTS = 0;
    // This is the only bounded hardware wait at priority 0: DISABLE is a
    // hardware transition (microseconds). End 200 us before the grant expires.
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {}
    NVIC_DisableIRQ(RADIO_IRQn);
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    state = RF_IDLE;
}
static void radio_event(void) {
    if (!NRF_RADIO->EVENTS_DISABLED) return;
    NRF_RADIO->EVENTS_DISABLED = 0;
    bool complete = NRF_RADIO->EVENTS_END != 0;
    NRF_RADIO->EVENTS_END = 0;
    if (now_us() >= STOP_US - 500) return;
    if (state == RF_WAIT_ACK || state == RF_LISTEN) {
        last_rx_length = rx.length; last_rx_s1 = rx.s1; last_rx_match = NRF_RADIO->RXMATCH;
        if (complete && !NRF_RADIO->CRCSTATUS) details[2]++;
    }
    if (state == RF_SEND_DATA) {
        // DISABLED_RXEN already started RX ramp-up. Set its buffer before
        // READY, and remove the shortcut so the ACK end does not restart RX.
        NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
        state = RF_WAIT_ACK;
        NRF_RADIO->PACKETPTR = (uint32_t)(uintptr_t)&rx;
        NRF_RADIO->EVENTS_END = NRF_RADIO->EVENTS_DISABLED = 0;
        NRF_TIMER0->EVENTS_COMPARE[1] = 0;
        NRF_TIMER0->CC[1] = now_us() + ACK_WAIT_US;
        NRF_TIMER0->INTENSET = IRQ_TIMER_ACK;
    } else if (state == RF_WAIT_ACK) {
        // Echo the full boot nonce and sequence, not just ESB's 2-bit PID.
        bool data_ack = (rx.length == NRF5_ESB_ACK_SIZE || rx.length == 64) && rx.data[0] == 0xA2 && !memcmp(rx.data + 1, tx.data + 4, 12);
        bool pair_ack = (rx.length == 24 || rx.length == 64) && rx.data[0] == 0xA3 && rx.data[1] == 3 && !memcmp(rx.data + 4, tx.data + 4, 12);
        if (complete && NRF_RADIO->CRCSTATUS && rx.s1 == tx.s1 && (data_ack || pair_ack)) {
            last_ack_length = rx.length; memcpy(last_ack, rx.data, rx.length);
            tx_acked = true; state = RF_IDLE;
            counters[2]++;
            NRF_TIMER0->INTENCLR = IRQ_TIMER_ACK;
        } else {
            details[3]++;
            last_ack_reject = (!complete ? 16 : 0) | (!NRF_RADIO->CRCSTATUS ? 1 : 0) |
                (rx.s1 != tx.s1 ? 2 : 0) | (rx.length != NRF5_ESB_ACK_SIZE && rx.length != 64 && rx.length != 24 ? 4 : 0) |
                (!data_ack && !pair_ack ? 8 : 0);
            listen(RF_WAIT_ACK);
        }
    } else if (state == RF_SEND_ACK) {
        if (complete) details[1]++;
        listen(RF_LISTEN);
    } else if (state == RF_LISTEN) {
        uint8_t next = (rx_tail + 1) % NRF5_ESB_QUEUE_SIZE;
        if (complete && NRF_RADIO->CRCSTATUS && enabled && (rx.s1 & 1) &&
            (nrf5_esb_envelope(rx.data, rx.length) || (pair_listening && nrf5_pair_envelope(rx.data, rx.length)))) {
            // Only thread-mode authenticated packets get ACKs. Cache the full
            // frame, so an altered ciphertext with the same counter is not ACKed.
            uint8_t index = cache_index;
            if (cache_ready && cached[index].length == rx.length && !memcmp(cached[index].packet, rx.data, rx.length)) {
                details[0]++;
                ack.length = cached[index].reply_length; ack.s1 = rx.s1;
                memcpy(ack.data, cached[index].reply, ack.length);
                transmit(&ack, RF_SEND_ACK);
                return;
            }
            if (next == rx_head) { listen(RF_LISTEN); return; }
            details[4]++;
            received[rx_tail] = rx;
            counters[3]++;
            __DMB(); rx_tail = next;
            listen(RF_LISTEN);
        } else listen(RF_LISTEN);
    }
}
static nrf_radio_signal_callback_return_param_t *signal_callback(uint8_t signal) {
    action.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_NONE;
    switch (signal) {
        case NRF_RADIO_CALLBACK_SIGNAL_TYPE_START:
            counters[0]++;
            NRF_TIMER0->INTENCLR = 0xFFFFFFFF;
            NRF_TIMER0->EVENTS_COMPARE[0] = NRF_TIMER0->EVENTS_COMPARE[1] = 0;
            NRF_TIMER0->CC[0] = STOP_US;
            NRF_TIMER0->INTENSET = IRQ_TIMER_END;
            NVIC_EnableIRQ(TIMER0_IRQn);
            setup_radio();
#ifdef NRF5_ESB_TX
            transmit(&tx, RF_SEND_DATA);
#else
            listen(RF_LISTEN);
#endif
            break;
        case NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO:
            radio_event();
#ifdef NRF5_ESB_TX
            if (tx_acked) {
                shutdown_radio();
                NVIC_DisableIRQ(TIMER0_IRQn);
                NVIC_ClearPendingIRQ(TIMER0_IRQn);
                action.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
            }
#endif
            break;
        case NRF_RADIO_CALLBACK_SIGNAL_TYPE_TIMER0:
            if (NRF_TIMER0->EVENTS_COMPARE[0]) {
                NRF_TIMER0->EVENTS_COMPARE[0] = 0;
                shutdown_radio();
                NVIC_DisableIRQ(TIMER0_IRQn);
                NVIC_ClearPendingIRQ(TIMER0_IRQn);
#ifdef NRF5_ESB_RX
                // S140 arbitrates each new grant with flash/calibration work.
                if (enabled) {
                    action.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_REQUEST_AND_END;
                    action.params.request.p_next = &request;
                } else action.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
#else
                action.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
#endif
            } else if (NRF_TIMER0->EVENTS_COMPARE[1]) {
                NRF_TIMER0->EVENTS_COMPARE[1] = 0;
                NRF_TIMER0->INTENCLR = IRQ_TIMER_ACK;
                if (state == RF_WAIT_ACK && now_us() < STOP_US - 600) {
                    state = RF_IDLE;
                    NRF_RADIO->TASKS_DISABLE = 1;
                    while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {}
                    transmit(&tx, RF_SEND_DATA);
                }
            }
            break;
        default: break;
    }
    return &action;
}
static void soc_event(uint32_t event, void *context) {
    (void)context;
    if (event == NRF_EVT_RADIO_SESSION_IDLE || event == NRF_EVT_RADIO_BLOCKED || event == NRF_EVT_RADIO_CANCELED) {
        if (event != NRF_EVT_RADIO_SESSION_IDLE) counters[4]++;
        session_idle = true;
#ifdef NRF5_ESB_TX
        if (tx_pending) result_ready = true;
#endif
    } else if (event == NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN || event == NRF_EVT_RADIO_SESSION_CLOSED) {
        radio_error = event;
    }
}
NRF_SDH_SOC_OBSERVER(esb_soc_observer, 1, soc_event, NULL);
void nrf5_esb_radio_init(void) {
    APP_ERROR_CHECK(sd_radio_session_open(signal_callback));
    session_idle = true;
}
void nrf5_esb_radio_task(void) {
#ifdef NRF5_ESB_RX
    if (session_idle && enabled && !radio_error) {
        session_idle = false;
        uint32_t rc = sd_radio_request(&request);
        if (rc != NRF_SUCCESS) { session_idle = true; radio_error = rc; }
    }
#endif
}
bool nrf5_esb_radio_send(const uint8_t *packet, uint8_t length) {
    if (!enabled || !session_idle || tx_pending || radio_error || !packet || length < NRF5_ESB_HEADER_SIZE || length > NRF5_ESB_PAYLOAD_MAX) return false;
    tx.length = length; tx.s1 = ((packet[12] & 3) << 1) | 1;
    memcpy(tx.data, packet, length);
    tx_acked = result_ready = false; tx_pending = true; session_idle = false;
    __DMB();
    uint32_t rc = sd_radio_request(&request);
    if (rc != NRF_SUCCESS) { tx_pending = false; session_idle = true; radio_error = rc; return false; }
    return true;
}
bool nrf5_esb_radio_result(bool *acknowledged) {
    if (!result_ready) return false;
    *acknowledged = tx_acked;
    tx_pending = result_ready = false;
    return true;
}
bool nrf5_esb_radio_receive(uint8_t *packet, uint8_t *length) {
    if (rx_head == rx_tail) return false;
    __DMB();
    *length = received[rx_head].length;
    memcpy(packet, received[rx_head].data, *length);
    __DMB(); rx_head = (rx_head + 1) % NRF5_ESB_QUEUE_SIZE;
    return true;
}
void nrf5_esb_radio_ack(uint8_t data[NRF5_ESB_ACK_SIZE]) {
    if ((last_ack_length == NRF5_ESB_ACK_SIZE || last_ack_length == 64) && last_ack[0]==0xA2) memcpy(data, last_ack, NRF5_ESB_ACK_SIZE);
    else memset(data, 0, NRF5_ESB_ACK_SIZE);
}
void nrf5_esb_radio_cache_ack(const uint8_t *packet, uint8_t length, const uint8_t reply[NRF5_ESB_ACK_SIZE]) {
    nrf5_esb_radio_cache_reply(packet, length, reply, NRF5_ESB_ACK_SIZE);
}
uint8_t nrf5_esb_radio_reply(uint8_t reply[64]) { memcpy(reply, last_ack, last_ack_length); return last_ack_length; }
void nrf5_esb_radio_cache_reply(const uint8_t *packet, uint8_t length, const uint8_t *reply, uint8_t reply_length) {
    if (length > 64 || reply_length > 64 || reply_length < 24) return;
    details[5]++;
    uint8_t index = cache_index ^ 1;
    cached[index].length = length; cached[index].reply_length = reply_length;
    memcpy(cached[index].packet, packet, length); memcpy(cached[index].reply, reply, reply_length);
    __DMB(); cache_index = index; cache_ready = true;
}
void nrf5_esb_radio_listen_pair(bool value) { pair_listening = value; }
void nrf5_esb_radio_configure(bool active, uint32_t value) {
    if (enabled != active || address != value) {
        enabled = false; cache_ready = false;
        rx_head = rx_tail;
        __DMB(); address = value; enabled = active;
    }
}
uint32_t nrf5_esb_radio_error(void) { return radio_error; }
void nrf5_esb_radio_counters(uint32_t values[5]) {
    for (uint8_t i = 0; i < 5; i++) values[i] = counters[i];
}

void nrf5_esb_radio_diagnostics(uint8_t data[32]) {
    memset(data + 1, 0, 31); data[1] = 1;
#ifdef NRF5_ESB_TX
    data[2] = 1;
#else
    data[2] = 2;
#endif
    data[3] = last_ack_reject;
    for (unsigned i = 0; i < 6; i++) {
        uint32_t value = details[i];
        for (unsigned b = 0; b < 4; b++) data[4 + i*4 + b] = value >> (24 - b*8);
    }
    data[28] = last_rx_length; data[29] = last_rx_s1; data[30] = last_rx_match; data[31] = tx.s1;
}
