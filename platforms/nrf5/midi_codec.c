// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "midi_transport.h"

static bool sysex;
static uint8_t sx[3], sx_count;
static const uint8_t lengths[16] = {0,0,2,3,3,1,2,3,3,3,3,3,2,2,3,1};

void nrf_midi_codec_reset(void) { sysex = false; sx_count = 0; }

static void emit(uint8_t cin, uint8_t a, uint8_t b, uint8_t c) {
    const uint8_t event[4] = {cin, a, b, c};
    nrf_midi_receive(event);
}

static void sysex_byte(uint8_t byte) {
    sx[sx_count++] = byte;
    if (byte == 0xf7 || sx_count == 3) {
        emit(byte == 0xf7 ? 4 + sx_count : 4, sx[0], sx_count > 1 ? sx[1] : 0, sx_count > 2 ? sx[2] : 0);
        sx_count = 0;
    }
    if (byte == 0xf7) sysex = false;
}

// BLE-MIDI 1.0. Timestamps are consumed; local playback follows arrival time.
// Running status is packet-local; SysEx may span packets. Real-time messages
// never overwrite an in-progress message or running status.
void nrf_midi_ble_receive(const uint8_t *data, size_t size) {
    if (size < 2 || (data[0] & 0xc0) != 0x80) return;
    uint8_t running = 0, message[3] = {0}, count = 0, needed = 0;
    size_t i = 1;
    while (i < size) {
        uint8_t b = data[i++];
        if (b & 0x80) {
            // Every explicit status (including an interleaved real-time byte
            // and SysEx end) is preceded by a timestampLow byte.
            if (i == size) goto malformed;
            b = data[i++];
            if (b >= 0xf8) {
                emit(15, b, 0, 0);
                continue;
            }
            if (sysex) {
                if (b != 0xf7) goto malformed;
                sysex_byte(b);
                continue;
            }
            if (count) goto malformed;
            if (b >= 0x80) {
                if (b == 0xf0) {
                    running = 0;
                    sysex = true;
                    sx_count = 0;
                    sysex_byte(b);
                    continue;
                }
                if (b == 0xf6) { running = 0; emit(5, b, 0, 0); continue; }
                if (b == 0xf4 || b == 0xf5 || b == 0xf7) goto malformed;
                running = b < 0xf0 ? b : 0;
                message[0] = b;
                count = 1;
                needed = b == 0xf1 || b == 0xf3 || (b & 0xe0) == 0xc0 ? 2 : 3;
                continue;
            }
            // Timestamp followed by data is valid running status.
        }
        if (sysex) { sysex_byte(b); continue; }
        if (!count) {
            if (!running) goto malformed;
            message[0] = running;
            count = 1;
            needed = (running & 0xe0) == 0xc0 ? 2 : 3;
        }
        message[count++] = b;
        if (count == needed) {
            uint8_t status = message[0];
            uint8_t cin = status < 0xf0 ? status >> 4 : (needed == 2 ? 2 : 3);
            emit(cin, status, message[1], needed == 3 ? message[2] : 0);
            count = 0;
        }
    }
    if (!count) return;
malformed:
    nrf_midi_codec_reset();
    nrf_midi_reset(); // Never leave a note held after losing framing.
}

size_t nrf_midi_ble_encode(const uint8_t packet[4], uint16_t time, uint8_t data[6]) {
    uint8_t cin = packet[0] & 15, length = lengths[cin];
    if (!length || (packet[0] >> 4)) return 0;
    size_t used = 0;
    data[used++] = 0x80 | ((time >> 7) & 0x3f);
    bool sx_packet = cin >= 4 && cin <= 7 && packet[1] != 0xf6;
    for (uint8_t i = 0; i < length; i++) {
        uint8_t b = packet[i + 1];
        if ((!sx_packet && i == 0) || (sx_packet && (b == 0xf0 || b == 0xf7)))
            data[used++] = 0x80 | (time & 0x7f);
        data[used++] = b;
    }
    return used;
}
