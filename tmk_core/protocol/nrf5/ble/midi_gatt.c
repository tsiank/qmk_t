// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ble.h"
#include "ble_srv_common.h"
#include "nrf_sdh_ble.h"
#include "app_error.h"
#include "midi_transport.h"
#include "ble_service.h"
#include "app_timer.h"

static uint16_t service_handle, connection = BLE_CONN_HANDLE_INVALID;
static ble_gatts_char_handles_t characteristic;
static bool subscribed;
ble_uuid_t nrf_midi_service_uuid;
static uint32_t last_ticks, elapsed_ticks;

void nrf_midi_ble_tick(void) {
    // QMK's legacy nRF timer divides RTC ticks by 32 (1024 "ms"/second).
    // BLE MIDI requires real milliseconds. Extend the 24-bit RTC every main
    // loop so its 512-second wrap does not introduce a timestamp discontinuity.
    uint32_t ticks = app_timer_cnt_get();
    elapsed_ticks += app_timer_cnt_diff_compute(ticks, last_ticks);
    last_ticks = ticks;
}

static uint16_t midi_timestamp(void) {
    // The uint32_t tick wrap is an integer multiple of the 8192-ms MIDI wrap.
    return (uint16_t)(((uint64_t)elapsed_ticks * 1000 / APP_TIMER_CLOCK_FREQ) & 0x1fff);
}

static void midi_event(ble_evt_t const *event, void *context) {
    (void)context;
    if (event->header.evt_id == BLE_GAP_EVT_CONNECTED) {
        connection = event->evt.gap_evt.conn_handle;
        subscribed = false;
    } else if (event->header.evt_id == BLE_GAP_EVT_DISCONNECTED) {
        connection = BLE_CONN_HANDLE_INVALID;
        subscribed = false;
        extern void nrf_midi_codec_reset(void);
        nrf_midi_codec_reset();
        nrf_midi_reset();
    } else if (event->header.evt_id == BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST) {
        const ble_gatts_evt_rw_authorize_request_t *request = &event->evt.gatts_evt.params.authorize_request;
        if (request->type == BLE_GATTS_AUTHORIZE_TYPE_READ && request->request.read.handle == characteristic.value_handle) {
            // BLE-MIDI's read handshake always returns an empty value, even
            // after notifications/writes have updated the attribute buffer.
            ble_gatts_rw_authorize_reply_params_t reply = {0};
            reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
            reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
            reply.params.read.update = 1;
            (void)sd_ble_gatts_rw_authorize_reply(connection, &reply);
        }
    } else if (event->header.evt_id == BLE_GATTS_EVT_WRITE) {
        const ble_gatts_evt_write_t *write = &event->evt.gatts_evt.params.write;
        if (write->handle == characteristic.cccd_handle && write->len == 2) {
            subscribed = ble_srv_is_notification_enabled(write->data);
            if (!subscribed) nrf_midi_reset();
        } else if (write->handle == characteristic.value_handle && !write->offset &&
                   (write->op == BLE_GATTS_OP_WRITE_CMD || write->op == BLE_GATTS_OP_WRITE_REQ)) {
            nrf_midi_ble_receive(write->data, write->len);
            reset_power_save_counter();
        }
    }
}
NRF_SDH_BLE_OBSERVER(midi_observer, 3, midi_event, NULL);

void nrf_midi_gatt_init(void) {
    // Standard BLE MIDI service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700.
    const ble_uuid128_t service_base = {{0x00,0xc7,0xc4,0x4e,0xe3,0x6c,0x51,0xa7,0x33,0x4b,0xe8,0xed,0x00,0x00,0xb8,0x03}};
    // Standard MIDI I/O characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3.
    const ble_uuid128_t io_base = {{0xf3,0x6b,0x10,0x9d,0x66,0xf2,0xa9,0xa1,0x12,0x41,0x68,0x38,0x00,0x00,0x72,0x77}};
    uint8_t service_type, io_type;
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&service_base, &service_type));
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&io_base, &io_type));
    nrf_midi_service_uuid = (ble_uuid_t){.uuid = 0x0e5a, .type = service_type};
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &nrf_midi_service_uuid, &service_handle));
    ble_add_char_params_t params = {0};
    params.uuid = 0xe5db;
    params.uuid_type = io_type;
    params.max_len = NRF_SDH_BLE_GATT_MAX_MTU_SIZE - 3;
    params.is_var_len = true;
    params.char_props.read = 1;
    params.is_defered_read = true;
    params.char_props.write = 1;
    params.char_props.write_wo_resp = 1;
    params.char_props.notify = 1;
    params.read_access = SEC_JUST_WORKS;
    params.write_access = SEC_JUST_WORKS;
    params.cccd_write_access = SEC_JUST_WORKS;
    APP_ERROR_CHECK(characteristic_add(service_handle, &params, &characteristic));
}

bool nrf_midi_ble_send(const uint8_t packet[4]) {
    // No MIDI subscriber: discard instead of replaying stale notes on subscribe.
    if (connection == BLE_CONN_HANDLE_INVALID) return true;
    // Bonded peers can restore CCCDs without sending another write event.
    uint8_t cccd[2] = {0};
    ble_gatts_value_t value = {.len = sizeof(cccd), .offset = 0, .p_value = cccd};
    ret_code_t state = sd_ble_gatts_value_get(connection, characteristic.cccd_handle, &value);
    subscribed = state == NRF_SUCCESS && value.len == 2 && ble_srv_is_notification_enabled(cccd);
    if (!subscribed) return true;
    uint8_t data[6];
    uint16_t length = nrf_midi_ble_encode(packet, midi_timestamp(), data);
    if (!length) return true;
    ble_gatts_hvx_params_t params = {0};
    params.handle = characteristic.value_handle;
    params.type = BLE_GATT_HVX_NOTIFICATION;
    params.p_len = &length;
    params.p_data = data;
    ret_code_t error = sd_ble_gatts_hvx(connection, &params);
    return error != NRF_ERROR_RESOURCES && error != NRF_ERROR_BUSY;
}

unsigned nrf_midi_ble_state(void) {
    return (connection != BLE_CONN_HANDLE_INVALID) | (subscribed << 1);
}
