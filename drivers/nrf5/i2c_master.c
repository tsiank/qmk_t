#include <string.h>

#include "i2c_master.h"
#include "nrfx_twim.h"
#include "nrf_log.h"
#include "nrf_pwr_mgmt.h"
#include "timer.h"

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
static bool              twim_initialized;
static uint8_t transmit_buffer[255];
static uint8_t receive_buffer[255];
static uint32_t transfer_errors;

uint32_t nrf5_i2c_error_count(void) { return transfer_errors; }

void twim_evt_handler(nrfx_twim_evt_t const* p_event, void* p_context) { twim_complete = p_event->type + 1; }

void i2c_init(void) {
    // OLED and matrix drivers share one TWIM instance. Display retries must
    // not reinitialize the peripheral or repeat the LED wake sequence.
    if (twim_initialized) {
        if (!twim_enable) i2c_start();
        return;
    }
    const nrfx_twim_config_t config = {.scl = I2C1_SCL, .sda = I2C1_SDA, .frequency = CONFIG_I2C_FREQ, .interrupt_priority = 5, .hold_bus_uninit = true};
    int                      res    = (int)nrfx_twim_init(&m_twim_master, &config, twim_evt_handler, NULL);
    if (res != NRFX_SUCCESS) { ++transfer_errors; return; }
    twim_initialized = true;
    i2c_start();
}

#if defined(RGB_MATRIX_IS31FL3733) || defined(RGB_MATRIX_IS31FL3737) || defined(RGB_MATRIX_IS31FL3741)
#    ifdef RGB_MATRIX_IS31FL3741
#        define NRF5_LED_FUNCTION_PAGE 0x04
#    else
#        define NRF5_LED_FUNCTION_PAGE 0x03
#    endif
static void i2c_write_register_byte(uint8_t addr, uint8_t reg, uint8_t data) {
    uint8_t bytes[2] = {reg, data};
    i2c_transmit(addr << 1, bytes, sizeof(bytes), 100);
}

#endif

void i2c_start(void) {
    if (!twim_initialized) return;
    if (!twim_enable) nrfx_twim_enable(&m_twim_master);
    twim_enable = true;

#if defined(RGB_MATRIX_IS31FL3733) || defined(RGB_MATRIX_IS31FL3737) || defined(RGB_MATRIX_IS31FL3741)
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFE, 0xC5);
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFD, NRF5_LED_FUNCTION_PAGE);
    i2c_write_register_byte(DRIVER_ADDR_1, 0x00, 0x01);

     #ifdef DRIVER_2_LED_TOTAL
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFE, 0xC5);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFD, NRF5_LED_FUNCTION_PAGE);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0x00, 0x01);
    #endif

#endif
}

void i2c_stop(void) {
    if (!twim_enable) return;
#if defined(RGB_MATRIX_IS31FL3733) || defined(RGB_MATRIX_IS31FL3737) || defined(RGB_MATRIX_IS31FL3741)
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFE, 0xC5);
    i2c_write_register_byte(DRIVER_ADDR_1, 0xFD, NRF5_LED_FUNCTION_PAGE);
    i2c_write_register_byte(DRIVER_ADDR_1, 0x00, 0x00);

     #ifdef DRIVER_2_LED_TOTAL
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFE, 0xC5);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0xFD, NRF5_LED_FUNCTION_PAGE);
	    i2c_write_register_byte(DRIVER_ADDR_2, 0x00, 0x00);
    #endif

#endif
#ifndef OLED_ENABLE
    // A light-off request must not disconnect the display sharing this bus.
    twim_enable = false;
    nrfx_twim_disable(&m_twim_master);
#endif
}

static i2c_status_t wait_for_transfer(uint16_t timeout) {
    uint32_t started = timer_read32();
    uint16_t limit = timeout ? timeout : 100; // Preserve legacy zero-timeout callers.
    while (twim_complete == 0) {
        if (timeout != I2C_TIMEOUT_INFINITE && timer_elapsed32(started) >= limit) {
            ++transfer_errors;
            return I2C_STATUS_TIMEOUT;
        }
        nrf_pwr_mgmt_run();
    }
    if (twim_complete != 1) {
        ++transfer_errors;
        return I2C_STATUS_ERROR;
    }
    return I2C_STATUS_SUCCESS;
}

i2c_status_t i2c_transmit(uint8_t address, const uint8_t* data, uint16_t length, uint16_t timeout) {
    if (!twim_enable || !data || length == 0 || length > sizeof(transmit_buffer)) return I2C_STATUS_ERROR;
    if (nrfx_twim_is_busy(&m_twim_master)) { ++transfer_errors; return I2C_STATUS_ERROR; }
    // OLED commands can live in Flash; EasyDMA only accepts RAM. This buffer
    // also outlives a timed-out caller and is not reused while DMA remains busy.
    memcpy(transmit_buffer, data, length);
    twim_complete = 0;
    if (nrfx_twim_tx(&m_twim_master, address >> 1, transmit_buffer, length, false) != NRFX_SUCCESS) {
        ++transfer_errors;
        return I2C_STATUS_ERROR;
    }
    return wait_for_transfer(timeout);
}

i2c_status_t i2c_receive(uint8_t address, uint8_t* data, uint16_t length, uint16_t timeout) {
    if (!twim_enable || !data || length == 0 || length > sizeof(receive_buffer)) return I2C_STATUS_ERROR;
    if (nrfx_twim_is_busy(&m_twim_master)) { ++transfer_errors; return I2C_STATUS_ERROR; }
    twim_complete = 0;
    if (nrfx_twim_rx(&m_twim_master, address >> 1, receive_buffer, length) != NRFX_SUCCESS) {
        ++transfer_errors;
        return I2C_STATUS_ERROR;
    }
    i2c_status_t status = wait_for_transfer(timeout);
    if (status == I2C_STATUS_SUCCESS) memcpy(data, receive_buffer, length);
    return status;
}

i2c_status_t i2c_write_register(uint8_t address, uint8_t reg, const uint8_t* data, uint16_t length, uint16_t timeout) {
    // EasyDMA transfers are limited to 255 bytes, including the register address.
    if (length > 254 || (!data && length)) {
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
    if (length) memcpy(buffer + 1, data, length);
    return i2c_transmit(address, buffer, length + 1, timeout) == 0 ? I2C_STATUS_SUCCESS : I2C_STATUS_ERROR;
}
