// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdbool.h>
enum { NRF5_MODE_AUTO, NRF5_MODE_USB, NRF5_MODE_BLE, NRF5_MODE_ESB, NRF5_MODE_NONE };
typedef struct {
    uint32_t magic;
    uint8_t key[16];
    uint32_t address;
    uint8_t mode, paired, reserved[2];
    uint32_t crc;
} nrf5_esb_settings_t;
void nrf5_esb_settings_init(void);
void nrf5_esb_settings_task(void);
const nrf5_esb_settings_t *nrf5_esb_settings(void);
bool nrf5_esb_settings_pair(const uint8_t key[16], uint32_t address);
void nrf5_esb_settings_clear(void);
bool nrf5_esb_settings_mode(uint8_t mode);
bool nrf5_esb_settings_pending(void);
uint32_t nrf5_esb_settings_error(void);
