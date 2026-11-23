// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "via_transport.h"
#ifdef NRF5_ESB_ENABLE
#    include "nrf5_esb.h"
#    include "esb_radio.h"
#endif
#include "raw_hid.h"
#ifdef NRF5_CONFIG_ENABLE
#    include "features/battery.h"
#endif
#include "eeprom_nrf.h"
#include "app_util_platform.h"
#include <string.h>
#ifdef NRF5_ESB_BRIDGE_ENABLE
#    define NRF_VIA_SOURCE_MAX NRF_VIA_ESB
#else
#    define NRF_VIA_SOURCE_MAX NRF_VIA_BLE
#endif
#ifdef NRF5_LIGHTING_DIAGNOSTICS
void nrf5_ui_diagnostics(uint8_t *data);
void nrf5_lighting_diagnostics(uint8_t data[32]);
#endif
#ifdef NRF5_VIA_UI_ENABLE
#    include "nrf5_via_ui.h"
#endif

// SDK callbacks only copy bounded packets. VIA and EEPROM run in thread mode.
#define QUEUE_SIZE 4
typedef struct {
    uint8_t data[32], source;
    uint16_t sequence;
    uint32_t generation;
} request_t;
static request_t queue[QUEUE_SIZE], current;
static volatile uint8_t head, count;
static volatile uint32_t generation[3];
static uint8_t response[32], fragments[32];
static uint16_t fragment_sequence;
static bool first_fragment, replying, executing;

bool nrf_via_receive(const uint8_t *data, uint8_t length, uint8_t source, uint16_t sequence) {
    if (!data || length != 32 || source > NRF_VIA_SOURCE_MAX) return false;
    bool accepted = false;
    CRITICAL_REGION_ENTER();
    if (count < QUEUE_SIZE) {
        request_t *item = &queue[(head + count) % QUEUE_SIZE];
        memcpy(item->data, data, 32);
        item->source = source; item->sequence = sequence;
        item->generation = generation[source];
        count++;
        accepted = true;
    }
    CRITICAL_REGION_EXIT();
    return accepted;
}

bool nrf_via_usb_receive(const uint8_t *report, size_t capacity) {
    // The SDK getter returns allocated capacity, not the transfer length.
    // Its generic macro reserves 32 + 1 bytes even for our no-report-ID HID.
    // Interrupt OUT starts with the nonzero VIA command. SET_REPORT instead
    // puts the synthetic zero Report ID at [0] and the 32-byte payload at [1].
    if (!report || capacity < 32 || capacity > 33) return false;
    if (capacity == 33 && report[0] == 0) ++report;
    return nrf_via_receive(report, 32, NRF_VIA_USB, 0);
}

void nrf_via_ble_fragment(const uint8_t *data, uint8_t length) {
    // Two 19-byte writes work even with the default ATT MTU of 23.
    if (!data || length != 19) { first_fragment = false; return; }
    uint16_t sequence = data[0] | ((uint16_t)data[1] << 8);
    if (data[2] == 0) {
        memcpy(fragments, data + 3, 16);
        fragment_sequence = sequence; first_fragment = true;
    } else if (data[2] == 16 && first_fragment && sequence == fragment_sequence) {
        memcpy(fragments + 16, data + 3, 16);
        first_fragment = false;
        nrf_via_receive(fragments, 32, NRF_VIA_BLE, sequence);
    } else first_fragment = false;
}

void nrf_via_disconnect(uint8_t source) {
#ifdef NRF5_VIA_UI_ENABLE
    if (source <= NRF_VIA_SOURCE_MAX) nrf5_ui_disconnect(source);
#endif
    if (source > NRF_VIA_SOURCE_MAX) return;
    CRITICAL_REGION_ENTER();
    generation[source]++;
    if (source == NRF_VIA_BLE) first_fragment = false;
    CRITICAL_REGION_EXIT();
}

void nrf_via_send(uint8_t *data, uint8_t length) {
    // Route replies to their requester, regardless of the typing output mode.
    if (!executing || length != 32) return;
    memcpy(response, data, 32);
    replying = true;
}

void nrf_via_task(void) {
    if (replying) {
        if (current.generation != generation[current.source]) replying = false;
        else {
            bool sent;
#ifdef NRF5_ESB_BRIDGE_ENABLE
            if(current.source==NRF_VIA_ESB) {
                bool nrf5_esb_via_reply(const uint8_t *,uint16_t);
                sent=nrf5_esb_via_reply(response,current.sequence);
            } else
#endif
            sent=current.source == NRF_VIA_USB ? nrf_via_usb_reply(response) : nrf_via_ble_reply(response, current.sequence);
            if(sent)replying=false;
        }
        return;
    }
    bool available = false;
    CRITICAL_REGION_ENTER();
    if (count) {
        current = queue[head]; head = (head + 1) % QUEUE_SIZE; count--;
        available = true;
    }
    CRITICAL_REGION_EXIT();
    if (!available || current.generation != generation[current.source]) return;
    executing = true;
    uint8_t command = current.data[0];
    bool buffer_command = command == 0x0E || command == 0x0F || command == 0x12 || command == 0x13;
    bool key_command = command == 4 || command == 5;
    bool malformed = buffer_command && current.data[3] > 28;
#ifdef VIA_ENABLE
    malformed |= key_command && (current.data[1] >= DYNAMIC_KEYMAP_LAYER_COUNT || current.data[2] >= MATRIX_ROWS || current.data[3] >= MATRIX_COLS);
#else
    (void)key_command;
#endif
    if (malformed) {
        current.data[0] = 0xFF;
        nrf_via_send(current.data, 32);
#ifdef VIA_ENABLE
    } else if (current.data[0] == 0x81) {
        // Qmknrf5 matrix descriptor: verify a layout before any keymap access.
        memset(current.data + 1, 0, 31);
        current.data[1] = 1;
        current.data[2] = MATRIX_ROWS;
        current.data[3] = MATRIX_COLS;
#ifdef NRF5_VIA_UI_ENABLE
        current.data[4] = 1;
#endif
        nrf_via_send(current.data, 32);
#endif
#ifdef NRF5_ESB_ENABLE
    } else if (current.data[0] == 0x84) {
        nrf5_esb_command(current.data, current.source == NRF_VIA_USB);
        nrf_via_send(current.data, 32);
    } else if (current.data[0] == 0x87) {
        nrf5_esb_radio_diagnostics(current.data);
        nrf_via_send(current.data, 32);
    } else if (current.data[0] == 0x83) {
        nrf5_esb_diagnostics(current.data);
        nrf_via_send(current.data, 32);
#endif
#ifdef NRF5_LIGHTING_DIAGNOSTICS
    } else if (current.data[0] == 0x82) {
        nrf5_ui_diagnostics(current.data);
        nrf_via_send(current.data, 32);
    } else if (current.data[0] == 0x85) {
        nrf5_lighting_diagnostics(current.data);
        nrf_via_send(current.data, 32);
#endif
#ifdef NRF5_CONFIG_ENABLE
    } else if (current.data[0] == 0x86) {
        nrf5_battery_command(current.data);
        nrf_via_send(current.data, 32);
#endif
    } else if (current.data[0] == 0x80) {
        // Private extension: acknowledge persistence only after FDS completes.
        uint32_t error = nrf5_eeprom_last_error();
        current.data[1] = nrf5_eeprom_pending();
        for (uint8_t i = 0; i < 4; ++i) current.data[2 + i] = error >> (24 - i * 8);
        nrf_via_send(current.data, 32);
#if defined(NRF5_VIA_UI_ENABLE) && !defined(NRF5_LIGHTING_DIAGNOSTICS)
    } else if (current.data[0] == 0x82) {
        nrf5_ui_diagnostics(current.data);
        nrf_via_send(current.data, 32);
#endif
#ifdef NRF5_VIA_UI_ENABLE
    } else if (nrf5_ui_command(current.data, current.source)) {
        nrf_via_send(current.data, 32);
#endif
    } else raw_hid_receive(current.data, 32);
    executing = false;
}
