// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ble_power.h"
#include "wireless_power.h"
#include "timer.h"
#include "app_util_platform.h"
#include "nrf_error.h"

#ifndef BLE_HID_MIN_INTERVAL
#    define BLE_HID_MIN_INTERVAL 7.5
#endif
#ifndef BLE_HID_MAX_INTERVAL
#    define BLE_HID_MAX_INTERVAL 30
#endif
#ifndef BLE_HID_SLAVE_LATENCY
#    define BLE_HID_SLAVE_LATENCY 6
#endif
#ifndef BLE_HID_TIMEOUT
#    define BLE_HID_TIMEOUT 430
#endif
#ifndef NRF5_BLE_IDLE_LATENCY
#    define NRF5_BLE_IDLE_LATENCY 9
#endif
#ifndef NRF5_BLE_IDLE_TIMEOUT
#    define NRF5_BLE_IDLE_TIMEOUT 1000
#endif

// Keep the HID connection interval short in both profiles. A peripheral with
// data can attend the next event without waiting through its allowed latency.
static const ble_gap_conn_params_t profiles[2] = {
    {BLE_HID_MIN_INTERVAL / 1.25, BLE_HID_MAX_INTERVAL / 1.25, BLE_HID_SLAVE_LATENCY, BLE_HID_TIMEOUT / 10},
    {BLE_HID_MIN_INTERVAL / 1.25, BLE_HID_MAX_INTERVAL / 1.25, NRF5_BLE_IDLE_LATENCY, NRF5_BLE_IDLE_TIMEOUT / 10},
};
_Static_assert(NRF5_BLE_IDLE_TIMEOUT > 2 * (NRF5_BLE_IDLE_LATENCY + 1) * BLE_HID_MAX_INTERVAL,
               "BLE idle supervision timeout must exceed twice the maximum event spacing");

static volatile uint16_t connection = BLE_CONN_HANDLE_INVALID;
static volatile uint32_t generation;
static volatile ble_gap_conn_params_t actual;
static uint32_t seen_generation, connected_at, attempted_at;
static bool settled, awaiting;
static uint8_t target = 255, attempts, busy_attempts;

void nrf5_ble_power_event(const ble_evt_t *event) {
    const uint16_t handle = event->evt.gap_evt.conn_handle;
    if (event->header.evt_id == BLE_GAP_EVT_CONNECTED) {
        connection = handle;
        actual = event->evt.gap_evt.params.connected.conn_params;
        ++generation;
    } else if (event->header.evt_id == BLE_GAP_EVT_DISCONNECTED && connection == handle) {
        connection = BLE_CONN_HANDLE_INVALID;
        ++generation;
    } else if (event->header.evt_id == BLE_GAP_EVT_CONN_PARAM_UPDATE && connection == handle) {
        actual = event->evt.gap_evt.params.conn_param_update.conn_params;
    }
}

static bool matches(const ble_gap_conn_params_t *current, const ble_gap_conn_params_t *wanted) {
    return current->min_conn_interval >= wanted->min_conn_interval && current->max_conn_interval <= wanted->max_conn_interval &&
           current->slave_latency == wanted->slave_latency && current->conn_sup_timeout == wanted->conn_sup_timeout;
}

void nrf5_ble_power_task(void) {
    uint16_t handle;
    uint32_t revision;
    ble_gap_conn_params_t current;
    CRITICAL_REGION_ENTER();
    handle = connection; revision = generation; current = actual;
    CRITICAL_REGION_EXIT();
    if (seen_generation != revision) {
        seen_generation = revision;
        connected_at = timer_read32();
        settled = awaiting = false;
        target = 255;
    }
    if (handle == BLE_CONN_HANDLE_INVALID) return;
    // Give encryption/service setup the same initial grace as the old SDK
    // parameter timer. Latch it so RTC wrap cannot reopen that grace period.
    if (!settled) {
        if (timer_elapsed32(connected_at) < 5 * 1024) return;
        settled = true;
    }
    const uint8_t wanted = NRF5_BLE_DYNAMIC_IDLE_ENABLE && nrf5_wireless_idle() ? 1 : 0;
    if (target != wanted) {
        target = wanted;
        attempts = busy_attempts = 0;
        awaiting = false;
        attempted_at = timer_read32() - 1024;
    }
    if (matches(&current, &profiles[target])) { awaiting = false; return; }
    if (attempts >= 3 || busy_attempts >= 5) return;
    if (timer_elapsed32(attempted_at) < (awaiting ? 10 * 1024 : 1024)) return;
    ble_gap_conn_params_t request = profiles[target];
    const uint32_t result = sd_ble_gap_conn_param_update(handle, &request);
    // A disconnect/reconnect can preempt the SVC; never charge its result to
    // a new connection reusing the same handle.
    if (generation != revision) return;
    attempted_at = timer_read32();
    if (result == NRF_SUCCESS) {
        attempts++;
        awaiting = true;
    } else {
        busy_attempts++;
        awaiting = false;
        // Busy, rejected, unsupported or stale-handle requests are optional
        // optimization failures. Do not disconnect, assert or reset the board.
    }
}
