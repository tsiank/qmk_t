// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "midi_transport.h"
#include "app_util_platform.h"
#include "host.h"
#include "nrf5_usb.h"
#include "ble_service.h"
#ifdef AUDIO_ENABLE
#    include "audio.h"
#    include <math.h>
#endif
#include <string.h>

#define QUEUE_SIZE 128
static uint8_t rx[QUEUE_SIZE][4], tx[QUEUE_SIZE][4];
static uint8_t rx_read, rx_write, tx_read, tx_write, rx_budget;
static bool reset_pending;
static host_driver_t *last_driver;
static const uint8_t lengths[16] = {0,0,2,3,3,1,2,3,3,3,3,3,2,2,3,1};

void nrf_midi_reset(void) {
    CRITICAL_REGION_ENTER();
    rx_read = rx_write = 0;
    reset_pending = true;
    CRITICAL_REGION_EXIT();
}

void nrf_midi_receive(const uint8_t packet[4]) {
    uint8_t cin = packet[0] & 15;
    if ((packet[0] >> 4) || !lengths[cin]) return;
    if (cin >= 8 && cin <= 14) {
        if ((packet[1] >> 4) != cin || packet[2] >= 128 || (lengths[cin] == 3 && packet[3] >= 128)) return;
    } else {
        switch (cin) {
            case 2:
                if ((packet[1] != 0xf1 && packet[1] != 0xf3) || packet[2] >= 128) return;
                break;
            case 3:
                if (packet[1] != 0xf2 || packet[2] >= 128 || packet[3] >= 128) return;
                break;
            case 4:
                if ((packet[1] >= 128 && packet[1] != 0xf0) || packet[2] >= 128 || packet[3] >= 128) return;
                break;
            case 5:
                if (packet[1] != 0xf6 && packet[1] != 0xf7) return;
                break;
            case 6:
            case 7:
                if ((packet[1] >= 128 && packet[1] != 0xf0) || packet[lengths[cin]] != 0xf7) return;
                if (cin == 7 && packet[2] >= 128) return;
                break;
            case 15:
                if (packet[1] < 0xf8) return;
                break;
        }
    }
    CRITICAL_REGION_ENTER();
    uint8_t next = (rx_write + 1) % QUEUE_SIZE;
    if (next == rx_read) {
        rx_read = rx_write = 0;
        reset_pending = true;
    } else if (!reset_pending) {
        memcpy(rx[rx_write], packet, 4);
        rx_write = next;
    }
    CRITICAL_REGION_EXIT();
}

bool recv_midi_packet(MIDI_EventPacket_t *event) {
    bool available = false;
    CRITICAL_REGION_ENTER();
    if (!reset_pending && rx_budget && rx_read != rx_write) {
        rx_budget--;
        memcpy(event, rx[rx_read], 4);
        rx_read = (rx_read + 1) % QUEUE_SIZE;
        available = true;
    }
    CRITICAL_REGION_EXIT();
    return available;
}

void send_midi_packet(MIDI_EventPacket_t *event) {
    uint8_t next = (tx_write + 1) % QUEUE_SIZE;
    if (next == tx_read) {
        // Replace the backlog with all-sound-off on every channel. A lost note
        // off must never leave the host synth held indefinitely.
        tx_read = tx_write = 0;
        for (uint8_t ch = 0; ch < 16; ch++) {
            uint8_t panic[4] = {0x0b, 0xb0 | ch, 120, 0};
            memcpy(tx[tx_write++], panic, 4);
        }
        return;
    }
    memcpy(tx[tx_write], event, 4);
    tx_write = next;
}

#ifdef AUDIO_ENABLE
static uint16_t held[128], sustained[128];
static uint16_t sustain_channels;
static float frequency(uint8_t note) { return 440.0f * powf(2.0f, ((int)note - 69) / 12.0f); }
static void release_channel(uint8_t channel, bool immediate) {
    uint16_t mask = 1u << channel;
    for (unsigned note = 0; note < 128; note++) {
        bool was = (held[note] | sustained[note]) != 0;
        if (!immediate && (sustain_channels & mask)) sustained[note] |= held[note] & mask;
        held[note] &= ~mask;
        if (immediate) sustained[note] &= ~mask;
        if (was && !(held[note] | sustained[note])) stop_note(frequency(note));
    }
}
#endif

void nrf_midi_audio(uint8_t status, uint8_t a, uint8_t b) {
#ifdef AUDIO_ENABLE
    if (status == 0xfc || status == 0xff) {
        memset(held, 0, sizeof(held));
        memset(sustained, 0, sizeof(sustained));
        sustain_channels = 0;
        stop_all_notes();
        return;
    }
    if (a > 127 || b > 127) return;
    uint16_t mask = 1u << (status & 15);
    switch (status & 0xf0) {
        case 0x90:
            if (b) {
                held[a] |= mask;
                sustained[a] &= ~mask;
                // QMK deduplicates the pitch and moves repeated notes to the
                // top of its stack, preserving last-note priority.
                play_note(frequency(a), b / 8.0f);
                break;
            }
            // MIDI Note On with velocity zero is Note Off.
            __attribute__((fallthrough));
        case 0x80:
            if ((sustain_channels & mask) && (held[a] & mask)) sustained[a] |= mask;
            held[a] &= ~mask;
            if (!(held[a] | sustained[a])) stop_note(frequency(a));
            break;
        case 0xb0:
            if (a == 120 || a == 123) release_channel(status & 15, a == 120);
            if (a == 64 || a == 121) {
                if (a == 64 && b >= 64) sustain_channels |= mask;
                else {
                    sustain_channels &= ~mask;
                    for (unsigned note = 0; note < 128; note++) {
                        bool was = (held[note] | sustained[note]) != 0;
                        sustained[note] &= ~mask;
                        if (was && !(held[note] | sustained[note])) stop_note(frequency(note));
                    }
                }
            }
            break;
    }
#endif
}

void nrf_midi_task(void) {
    host_driver_t *driver = host_get_driver();
    if (last_driver != driver) {
        nrf_midi_reset();
        last_driver = driver;
    }
    bool reset;
    CRITICAL_REGION_ENTER();
    reset = reset_pending;
    reset_pending = false;
    rx_budget = 32;
    if (reset) nrf_midi_codec_reset();
    CRITICAL_REGION_EXIT();
    if (reset) {
        tx_read = tx_write = 0;
        nrf_midi_audio(0xff, 0, 0);
        midi_device.input_state = IDLE;
        midi_device.input_count = 0;
        bytequeue_init(&midi_device.input_queue, midi_device.input_queue_data, MIDI_INPUT_QUEUE_LENGTH);
    }
    nrf_midi_ble_tick();
    nrf_midi_usb_task();
    // Bounded work keeps scanning and lighting responsive under MIDI traffic.
    for (unsigned i = 0; i < 16 && tx_read != tx_write; i++) {
        bool sent;
        if (driver == nrf5_usb_driver()) sent = nrf_midi_usb_send(tx[tx_read]);
        else if (driver == nrf5_ble_driver()) sent = nrf_midi_ble_send(tx[tx_read]);
        else { tx_read = tx_write; break; }
        if (!sent) break;
        tx_read = (tx_read + 1) % QUEUE_SIZE;
    }
}
