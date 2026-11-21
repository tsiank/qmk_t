// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ble.h"
#include "ble_srv_common.h"
#include "nrf_sdh_ble.h"
#include "app_error.h"
#include "via_transport.h"
#include "ble_service.h"
#include <string.h>
// Service c1a00010-7d9b-4c7d-a568-7294e2166700, RX ...0011, state ...0012.
static ble_gatts_char_handles_t rx, state;
static void via_event(ble_evt_t const *event, void *context) {
    (void)context;
    if (event->header.evt_id == BLE_GATTS_EVT_WRITE) {
        const ble_gatts_evt_write_t *write = &event->evt.gatts_evt.params.write;
        if (write->handle == rx.value_handle && write->offset == 0 && write->op == BLE_GATTS_OP_WRITE_REQ) {
            nrf_via_ble_fragment(write->data, write->len);
            reset_power_save_counter();
        }
    } else if (event->header.evt_id == BLE_GAP_EVT_DISCONNECTED) nrf_via_disconnect(NRF_VIA_BLE);
}
NRF_SDH_BLE_OBSERVER(via_observer, 3, via_event, NULL);
void nrf_via_gatt_init(void) {
    const ble_uuid128_t base = {{0x00, 0x67, 0x16, 0xe2, 0x94, 0x72, 0x68, 0xa5,
                               0x7d, 0x4c, 0x9b, 0x7d, 0x00, 0x00, 0xa0, 0xc1}};
    uint8_t type; uint16_t handle;
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&base, &type));
    ble_uuid_t uuid = {.uuid = 0x10, .type = type};
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &uuid, &handle));
    uint8_t initial[34] = {0};
    ble_add_char_params_t c = {0};
    c.uuid = 0x11; c.uuid_type = type;
    c.max_len = c.init_len = 19; c.p_init_value = initial;
    c.char_props.write = 1; c.write_access = SEC_JUST_WORKS;
    APP_ERROR_CHECK(characteristic_add(handle, &c, &rx));
    ble_add_char_params_t r = {0};
    r.uuid = 0x12; r.uuid_type = type;
    r.max_len = r.init_len = 34; r.p_init_value = initial;
    r.char_props.read = 1; r.read_access = SEC_JUST_WORKS;
    APP_ERROR_CHECK(characteristic_add(handle, &r, &state));
}
bool nrf_via_ble_reply(const uint8_t *data, uint16_t sequence) {
    uint8_t packet[34] = {(uint8_t)sequence, (uint8_t)(sequence >> 8)};
    memcpy(packet + 2, data, 32);
    ble_gatts_value_t value = {.len = 34, .offset = 0, .p_value = packet};
    return sd_ble_gatts_value_set(BLE_CONN_HANDLE_INVALID, state.value_handle, &value) == NRF_SUCCESS;
}
