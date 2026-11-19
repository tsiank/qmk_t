#include <string.h>

#include "i2c_master.h"
#include "nrfx_twim.h"
#include "nrf_log.h"
#include "nrf_pwr_mgmt.h"

#ifndef I2C1_SCL
#    define I2C1_SCL 27
#endif
#ifndef I2C1_SDA
#    define I2C1_SDA 26
#endif
#ifndef CONFIG_I2C_FREQ
#    define CONFIG_I2C_FREQ NRF_TWIM_FREQ_400K
#endif

static const nrfx_twim_t m_twim_master = NRFX_TWIM_INSTANCE(0);
static volatile int      twim_complete = 0;
static volatile bool     twim_enable   = false;
static uint8_t twi_transfer_buffer[10];
static uint32_t transfer_errors;

uint32_t nrf5_i2c_error_count(void) { return transfer_errors; }

void twim_evt_handler(nrfx_twim_evt_t const* p_event, void* p_context) { twim_complete = p_event->type + 1; }

void i2c_init(void) {
    const nrfx_twim_config_t config = {.scl = I2C1_SCL, .sda = I2C1_SDA, .frequency = CONFIG_I2C_FREQ, .interrupt_priority = 5, .hold_bus_uninit = true};
    int                      res    = (int)nrfx_twim_init(&m_twim_master, &config, twim_evt_handler, NULL);
    // nrfx_twim_enable(&m_twim_master);
    i2c_start();
    (void)res;
}

static void i2c_write_register_byte(uint8_t addr, uint8_t reg, uint8_t data) {
    twi_transfer_buffer[0] = reg;
    twi_transfer_buffer[1] = data;
    i2c_transmit(addr << 1, twi_transfer_buffer, 2, 0);
}

void i2c_start(void) {
    nrfx_twim_enable(&m_twim_master);
    twim_enable = true;

#if defined(RGB_MATRIX_IS31FL3733) || defined(RGB_MATRIX_IS31FL3737) || defined(RGB_MATRIX_IS31FL3741)
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFE, 0xC5);
#ifdef RGB_MATRIX_IS31FL3741
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFD, 0x04);
#else
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFD, 0x03);
#endif
    i2c_write_register_byte(DRIVER_ADDR_1, 0x00, 0x01);

     #ifdef DRIVER_2_LED_TOTAL
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFE, 0xC5);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFD, 0x03);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0x00, 0x01);
    #endif

#endif
}

void i2c_stop(void) {
#if defined(RGB_MATRIX_IS31FL3733) || defined(RGB_MATRIX_IS31FL3737) || defined(RGB_MATRIX_IS31FL3741)
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFE, 0xC5);
#ifdef RGB_MATRIX_IS31FL3741
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFD, 0x04);
#else
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFD, 0x03);
#endif
    i2c_write_register_byte(DRIVER_ADDR_1, 0x00, 0x00);

     #ifdef DRIVER_2_LED_TOTAL
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFE, 0xC5);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFD, 0x03);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0x00, 0x00);
    #endif

#endif
    twim_enable = false;
    nrfx_twim_disable(&m_twim_master);
}

i2c_status_t i2c_transmit(uint8_t address, const uint8_t* data, uint16_t length, uint16_t timeout) {
    if (!twim_enable) {
        return I2C_STATUS_ERROR;
    }
    nrfx_err_t res;
    twim_complete = 0;

    res = nrfx_twim_tx(&m_twim_master, address >> 1, data, length, false);
    if (res != NRFX_SUCCESS) {
        NRF_LOG_INFO("Twim error:%d", res);
    }

    int cnt = 0;
    if (res == NRFX_SUCCESS) {
        while (twim_complete == 0) {
            nrf_pwr_mgmt_run();
            if (++cnt == 100) break;
        }
    }
    if (cnt == 100) {
        NRF_LOG_INFO("Twim tx timeout");
    }
    if (twim_complete != 1) {
        ++transfer_errors;
        NRF_LOG_INFO("twim_uncomplete:%d %d %d", length, data[0], data[1]);
    }
    return twim_complete == 1 ? I2C_STATUS_SUCCESS : I2C_STATUS_ERROR;
}

i2c_status_t i2c_receive(uint8_t address, uint8_t* data, uint16_t length, uint16_t timeout) {
    if (!twim_enable) {
        return I2C_STATUS_ERROR;
    }
    nrfx_err_t res;
    twim_complete = 0;
    res           = nrfx_twim_rx(&m_twim_master, address >> 1, data, length);
    int cnt       = 0;
    if (res == NRFX_SUCCESS) {
        while (twim_complete == 0) {
            nrf_pwr_mgmt_run();
            if (cnt++ == 100) break;
        }
    }
    if (cnt >= 100) NRF_LOG_INFO("Twim rx timeout");
    if (twim_complete != 1) ++transfer_errors;
    return twim_complete == 1 ? I2C_STATUS_SUCCESS : I2C_STATUS_ERROR;
}

i2c_status_t i2c_write_register(uint8_t address, uint8_t reg, const uint8_t* data, uint16_t length, uint16_t timeout) {
    // EasyDMA transfers are limited to 255 bytes, including the register address.
    if (length > 254) {
        return I2C_STATUS_ERROR;
    }
    static uint8_t buffer[255];
    // Keep the EasyDMA buffer alive after a timeout, and do not overwrite an
    // outstanding transfer on the next call.
    if (nrfx_twim_is_busy(&m_twim_master)) {
        ++transfer_errors;
        return I2C_STATUS_ERROR;
    }
    buffer[0] = reg;
    memcpy(buffer + 1, data, length);
    return i2c_transmit(address, buffer, length + 1, timeout) == 0 ? I2C_STATUS_SUCCESS : I2C_STATUS_ERROR;
}
