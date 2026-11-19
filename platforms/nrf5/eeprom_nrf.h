// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

// The old 36-byte store predates QMK's current core and keyboard/user blocks.
// Keep the capacity configurable for keymaps such as VIA.
#ifndef EEPROM_SIZE
#    ifdef VIA_ENABLE
#        define EEPROM_SIZE 1024
#    else
#        define EEPROM_SIZE 64
#    endif
#endif

uint32_t nrf5_eeprom_last_error(void);
uint32_t nrf5_eeprom_error_count(void);
