// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qmk_midi.h"
void nrf_midi_receive(const uint8_t packet[4]);
void nrf_midi_reset(void);
void nrf_midi_task(void);
void nrf_midi_audio(uint8_t status, uint8_t a, uint8_t b);
void nrf_midi_ble_receive(const uint8_t *data, size_t size);
size_t nrf_midi_ble_encode(const uint8_t packet[4], uint16_t time, uint8_t data[6]);
void nrf_midi_usb_init(void);
void nrf_midi_usb_task(void);
void nrf_midi_codec_reset(void);
bool nrf_midi_usb_send(const uint8_t packet[4]);
void nrf_midi_gatt_init(void);
void nrf_midi_ble_tick(void);
bool nrf_midi_ble_send(const uint8_t packet[4]);
void nrf_midi_diagnostics(char *buffer, size_t size);
unsigned nrf_midi_usb_state(void);
unsigned nrf_midi_ble_state(void);
