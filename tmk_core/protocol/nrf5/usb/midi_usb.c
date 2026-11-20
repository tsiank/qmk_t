// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "app_usbd.h"
#include "app_usbd_core.h"
#include "app_error.h"
#include "midi_transport.h"
#include <string.h>

#ifdef MUSIC_STREAM_ENABLE
#    define MIDI_CONTROL_INTERFACE 5
#else
#    define MIDI_CONTROL_INTERFACE 4
#endif
#define MIDI_STREAM_INTERFACE (MIDI_CONTROL_INTERFACE + 1)
#define MIDI_IN NRF_DRV_USBD_EPIN5
#define MIDI_OUT NRF_DRV_USBD_EPOUT5
#define MIDI_CONFIG ((MIDI_CONTROL_INTERFACE), (MIDI_STREAM_INTERFACE, MIDI_IN, MIDI_OUT))

// USB MIDI 1.0, one cable in each direction. Bulk endpoints are independent of
// the HID music visualizer's EP1. Contiguous AC + MS interfaces use the
// USB Audio 1.0 collection grouping (device class remains zero).
static const uint8_t descriptors[] = {
    9, 4, MIDI_CONTROL_INTERFACE, 0, 0, 1, 1, 0, 0,
    9, 0x24, 1, 0, 1, 9, 0, 1, MIDI_STREAM_INTERFACE,
    9, 4, MIDI_STREAM_INTERFACE, 0, 2, 1, 3, 0, 0,
    7, 0x24, 1, 0, 1, 65, 0,
    6, 0x24, 2, 1, 1, 0, // embedded IN jack, host -> device
    6, 0x24, 2, 2, 2, 0, // external IN jack
    9, 0x24, 3, 1, 3, 1, 2, 1, 0, // embedded OUT from external IN
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,
    9, 5, MIDI_OUT, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 1,
    9, 5, MIDI_IN, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 3,
};

static bool configured, suspended, tx_busy;
static uint8_t panic_remaining;
static uint8_t input[64], output[4];
static size_t input_size;

APP_USBD_CLASS_TYPEDEF(nrf_usb_midi, MIDI_CONFIG,
    APP_USBD_CLASS_INSTANCE_SPECIFIC_DEC_NONE, APP_USBD_CLASS_DATA_SPECIFIC_DEC_NONE);

static bool feed(app_usbd_class_descriptor_ctx_t *context,
                 app_usbd_class_inst_t const *instance, uint8_t *buffer, size_t size) {
    (void)instance;
    APP_USBD_CLASS_DESCRIPTOR_BEGIN(context, buffer, size);
    static size_t i;
    for (i = 0; i < sizeof(descriptors); i++) {
        APP_USBD_CLASS_DESCRIPTOR_WRITE(descriptors[i]);
    }
    APP_USBD_CLASS_DESCRIPTOR_END();
}

static ret_code_t select_interface(app_usbd_class_inst_t const *instance, uint8_t index, uint8_t alternate) {
    (void)instance;
    if (alternate) return NRF_ERROR_INVALID_PARAM;
    if (index == 1) {
        app_usbd_ep_enable(MIDI_IN);
        app_usbd_ep_enable(MIDI_OUT);
        configured = true;
        suspended = false;
        tx_busy = false;
    }
    return NRF_SUCCESS;
}

static void deselect_interface(app_usbd_class_inst_t const *instance, uint8_t index) {
    (void)instance;
    if (index == 1) {
        configured = false;
        tx_busy = false;
        nrf_midi_reset();
    }
}

static ret_code_t event_handler(app_usbd_class_inst_t const *instance, app_usbd_complex_evt_t const *event) {
    (void)instance;
    switch (event->app_evt.type) {
        case APP_USBD_EVT_DRV_SETUP:
            // Standard GET/SET_INTERFACE and endpoint requests use SDK defaults.
            return NRF_ERROR_NOT_SUPPORTED;
        case APP_USBD_EVT_DRV_SUSPEND:
            suspended = true;
            nrf_drv_usbd_ep_abort(MIDI_IN);
            tx_busy = false;
            nrf_midi_reset();
            break;
        case APP_USBD_EVT_DRV_RESUME:
            suspended = false;
            panic_remaining = 16; // stop host notes held before suspend
            break;
        case APP_USBD_EVT_DRV_RESET:
        case APP_USBD_EVT_STOPPED:
        case APP_USBD_EVT_POWER_REMOVED:
            configured = tx_busy = false;
            panic_remaining = 0;
            nrf_midi_reset();
            break;
        case APP_USBD_EVT_DRV_EPTRANSFER: {
            uint8_t ep = event->drv_evt.data.eptransfer.ep;
            nrf_drv_usbd_ep_status_t status = event->drv_evt.data.eptransfer.status;
            if (ep == MIDI_IN) {
                if (status == NRF_USBD_EP_OK || status == NRF_USBD_EP_ABORTED) tx_busy = false;
            } else if (ep == MIDI_OUT && status == NRF_USBD_EP_WAITING) {
                input_size = nrf_drv_usbd_epout_size_get(MIDI_OUT);
                if (input_size > sizeof(input)) return NRF_ERROR_INVALID_LENGTH;
                NRF_DRV_USBD_TRANSFER_OUT(transfer, input, input_size);
                return app_usbd_ep_transfer(MIDI_OUT, &transfer);
            } else if (ep == MIDI_OUT && status == NRF_USBD_EP_OK) {
                if (input_size % 4) { nrf_midi_reset(); break; }
                for (size_t i = 0; i < input_size; i += 4) nrf_midi_receive(input + i);
            }
            break;
        }
        default: break;
    }
    return NRF_SUCCESS;
}

static const app_usbd_class_methods_t methods = {
    .event_handler = event_handler, .feed_descriptors = feed,
    .iface_select = select_interface, .iface_deselect = deselect_interface,
};
APP_USBD_CLASS_INST_DEF(midi_instance, nrf_usb_midi, &methods, MIDI_CONFIG, ());

void nrf_midi_usb_init(void) {
    APP_ERROR_CHECK(app_usbd_class_append(&midi_instance.base));
}

static bool write_packet(const uint8_t packet[4]) {
    if (!configured || suspended) return true;
    if (tx_busy) return false;
    memcpy(output, packet, 4);
    NRF_DRV_USBD_TRANSFER_IN(transfer, output, sizeof(output));
    ret_code_t result = app_usbd_ep_transfer(MIDI_IN, &transfer);
    if (result == NRF_SUCCESS) tx_busy = true;
    return result == NRF_SUCCESS;
}

void nrf_midi_usb_task(void) {
    if (configured && !suspended && panic_remaining) {
        const uint8_t packet[4] = {0x0b, 0xb0 | (16 - panic_remaining), 120, 0};
        if (write_packet(packet)) panic_remaining--;
    }
}

bool nrf_midi_usb_send(const uint8_t packet[4]) {
    if (panic_remaining) return false;
    return write_packet(packet);
}

unsigned nrf_midi_usb_state(void) {
    return configured | (suspended << 1) | (tx_busy << 2) | ((panic_remaining != 0) << 3);
}
