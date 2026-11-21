// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

void nrf5_clock_set(uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisecond);
void nrf5_clock_task(void);
bool nrf5_clock_read(uint8_t *hour, uint8_t *minute, uint8_t *second);
// Two-hour branch: odd hour is 初, even hour is 正; four quarters per hour.
uint8_t nrf5_clock_branch(uint8_t hour);
bool nrf5_clock_is_zheng(uint8_t hour);
uint8_t nrf5_clock_quarter(uint8_t minute);
