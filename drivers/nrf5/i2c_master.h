#pragma once

#include "../i2c_master.h"

// Logical LED/bus control. TWIM hardware is enabled only for a transfer;
// completion releases it without changing OLED or LED device registers.
void i2c_start(void);
void i2c_stop(void);
uint32_t nrf5_i2c_error_count(void);
