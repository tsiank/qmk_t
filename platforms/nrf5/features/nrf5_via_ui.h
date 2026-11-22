// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define NRF5_UI_SIZE 1792
#define NRF5_UI_TITLE 224
#define NRF5_UI_LOGO 736
#define NRF5_UI_FONT 992
#define NRF5_UI_CRC 1788
#define NRF5_UI_WIDGETS 7
#define NRF5_UI_ENCODERS 4
#define NRF5_UI_LAYERS 8
// Wire layout is byte-addressed, little endian; no ABI-dependent C structs.
const uint8_t *nrf5_ui_config(void);
bool nrf5_ui_command(uint8_t *data, uint8_t source);
void nrf5_ui_disconnect(uint8_t source);
uint16_t nrf5_ui_encoder_key(uint8_t index, uint8_t layer, bool clockwise);
bool nrf5_ui_encoder(uint8_t index, uint8_t layer, uint8_t *action, uint16_t *cw, uint16_t *ccw);

void nrf5_ui_diagnostics(uint8_t *data);
