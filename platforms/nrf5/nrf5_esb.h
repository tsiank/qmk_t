// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "host_driver.h"

// Transport roles are build-time choices in the first hardware trial.
void nrf5_esb_init(void);
void nrf5_esb_task(void);
// Extended RTC time for pairing/link deadlines, sampled in main context.
uint32_t nrf5_esb_now(void);
host_driver_t *nrf5_esb_driver(void);
host_driver_t *nrf5_esb_quiet_driver(void);
void nrf5_esb_set_active(bool active);
#ifdef NRF5_ESB_TX
// Main-context delay in QMK RTC units (1/1024 s), not milliseconds.
uint32_t nrf5_esb_next_wake_ticks(void);
// Automatic routing can discover an existing receiver while another host owns
// input. Background probes contain HELLO only, never keyboard/mouse reports.
void nrf5_esb_set_probe(bool enabled);
#endif
void nrf5_esb_release(void);
bool nrf5_esb_idle(void);
bool nrf5_esb_connected(void);
void nrf5_esb_reset_link(void);
void nrf5_esb_diagnostics(uint8_t packet[32]);
// Optional board/USB observations, no key material or state mutation.
void nrf5_esb_led_diagnostics(uint8_t data[3]);
void nrf5_esb_command(uint8_t packet[32], bool usb);
// A transport driver, not a second matrix scanner. A future split transport
// can share the radio layer without merging independent HID reports.
