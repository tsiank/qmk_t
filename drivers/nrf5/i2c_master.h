#pragma once

#include "../i2c_master.h"

// nRF5 extensions for switching the LED driver and I2C peripheral off together.
void i2c_start(void);
void i2c_stop(void);
uint32_t nrf5_i2c_error_count(void);
