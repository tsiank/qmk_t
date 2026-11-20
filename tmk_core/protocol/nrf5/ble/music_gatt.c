// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ble.h"
#include "ble_srv_common.h"
#include "nrf_sdh_ble.h"
#include "app_error.h"
#include "music_stream.h"
#include "ble_service.h"

// c1a00001-7d9b-4c7d-a568-7294e2166700 (service), ...0002 (RX).
static uint16_t service_handle;
static ble_gatts_char_handles_t rx;
#ifdef KEYBOARD_CONTROL_ENABLE
static ble_gatts_char_handles_t control_state;
#endif

static void music_ble_event(ble_evt_t const *event, void *context) {
    (void)context;
    if (event->header.evt_id == BLE_GATTS_EVT_WRITE) {
        const ble_gatts_evt_write_t *write = &event->evt.gatts_evt.params.write;
        if (write->handle == rx.value_handle && write->offset == 0 &&
            (write->op == BLE_GATTS_OP_WRITE_REQ || write->op == BLE_GATTS_OP_WRITE_CMD)) {
            if (music_stream_receive(write->data, write->len, MUSIC_SOURCE_BLE)) {
                if ((write->data[1] == 1 && (write->data[3] & MUSIC_TARGET_MASK)) || write->data[1] == 3)
                    reset_power_save_counter();
            }
        }
    } else if (event->header.evt_id == BLE_GAP_EVT_DISCONNECTED) {
        music_stream_disconnect(MUSIC_SOURCE_BLE);
    }
}
NRF_SDH_BLE_OBSERVER(music_observer, 3, music_ble_event, NULL);

void music_gatt_init(void) {
    const ble_uuid128_t base = {{0x00, 0x67, 0x16, 0xe2, 0x94, 0x72, 0x68, 0xa5,
                               0x7d, 0x4c, 0x9b, 0x7d, 0x00, 0x00, 0xa0, 0xc1}};
    uint8_t type;
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&base, &type));
    ble_uuid_t service = {.uuid = 1, .type = type};
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service, &service_handle));
    ble_add_char_params_t characteristic = {0};
    uint8_t initial[MUSIC_PACKET_SIZE] = {0};
    characteristic.uuid = 2;
    characteristic.uuid_type = type;
    characteristic.max_len = MUSIC_PACKET_SIZE;
    characteristic.init_len = MUSIC_PACKET_SIZE;
    characteristic.p_init_value = initial;
    characteristic.char_props.write = 1;
    characteristic.char_props.write_wo_resp = 1;
    characteristic.write_access = SEC_JUST_WORKS;
    APP_ERROR_CHECK(characteristic_add(service_handle, &characteristic, &rx));
#ifdef KEYBOARD_CONTROL_ENABLE
    ble_add_char_params_t state = {0};
    state.uuid = 3; state.uuid_type = type;
    state.max_len = state.init_len = 20; state.p_init_value = initial;
    state.char_props.read = 1; state.read_access = SEC_JUST_WORKS;
    APP_ERROR_CHECK(characteristic_add(service_handle, &state, &control_state));
#endif
}

#ifdef KEYBOARD_CONTROL_ENABLE
bool keyboard_control_ble_reply(const uint8_t *data) {
    ble_gatts_value_t value = {.len = 20, .offset = 0, .p_value = (uint8_t *)data};
    ret_code_t result = sd_ble_gatts_value_set(BLE_CONN_HANDLE_INVALID, control_state.value_handle, &value);
    return result == NRF_SUCCESS;
}
#endif
