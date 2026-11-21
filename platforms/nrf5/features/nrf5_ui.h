// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quantum.h"

// QMK callback return convention: false means the event was handled.
// User callbacks run first and can replace the shared behavior by returning false.
bool nrf5_encoder_update(uint8_t index, bool clockwise);
bool nrf5_oled_task(void);
// RAM policy: 0 automatic (USB on / battery idle timeout), 1 always on, 2 off.
uint8_t nrf5_oled_get_power_mode(void);
void nrf5_oled_set_power_mode(uint8_t mode);
bool nrf5_oled_wake_allowed(void);
void nrf5_oled_power_task(void);

typedef enum {
    NRF5_ENCODER_KEYS,
    NRF5_ENCODER_UNDERGLOW,
    NRF5_ENCODER_MATRIX,
} nrf5_encoder_action_t;

typedef struct {
    uint8_t index;
    uint8_t layer;
    nrf5_encoder_action_t action;
    uint16_t clockwise;
    uint16_t counterclockwise;
} nrf5_encoder_binding_t;
