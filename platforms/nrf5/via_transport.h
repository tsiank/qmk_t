// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define NRF_VIA_PACKET_SIZE 32
#define NRF_VIA_USB 0
#define NRF_VIA_BLE 1
#define NRF_VIA_ESB 2
bool nrf_via_receive(const uint8_t *data, uint8_t length, uint8_t source, uint16_t sequence);
bool nrf_via_usb_receive(const uint8_t *report, size_t capacity);
void nrf_via_ble_fragment(const uint8_t *data, uint8_t length);
void nrf_via_task(void);
void nrf_via_disconnect(uint8_t source);
void nrf_via_send(uint8_t *data, uint8_t length);
bool nrf_via_usb_reply(const uint8_t *data);
bool nrf_via_ble_reply(const uint8_t *data, uint16_t sequence);
