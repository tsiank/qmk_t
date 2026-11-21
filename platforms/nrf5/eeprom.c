#include <string.h>
#include "eeprom.h"
#include "fds.h"
#include "nrf_log.h"
#include "eeprom_nrf.h"
#include "nvm_eeprom_eeconfig_internal.h"

#define FILE_ID 0x1000
#define RECORD_KEY 0x1001

STATIC_ASSERT(EEPROM_SIZE >= EECONFIG_SIZE, "nRF EEPROM is too small for QMK configuration");
STATIC_ASSERT(EEPROM_SIZE % sizeof(uint32_t) == 0, "nRF EEPROM must be word aligned");

__ALIGN(4)
static uint8_t           buffer[EEPROM_SIZE] __attribute__((aligned(4)));
static bool fds_inited;
static volatile bool record_exists;
static volatile bool fds_update;
static volatile uint32_t fds_update_counter;
static volatile bool write_pending;
static volatile bool gc_pending;
static volatile bool fds_init_complete;
static volatile ret_code_t fds_init_result;
static volatile ret_code_t last_fds_error;
static volatile uint32_t fds_error_count;

// FDS retains the source pointer until its completion event. Never let key
// events change an in-flight flash write's data.
static uint8_t write_buffer[EEPROM_SIZE] __attribute__((aligned(4)));

#ifndef FDS_UPDATE_TIMEOUT
#    define FDS_UPDATE_TIMEOUT 500
#endif

static fds_record_desc_t record_desc;
static fds_record_t const record = {
    .file_id = FILE_ID,
    .key = RECORD_KEY,
    .data.p_data = write_buffer,
    .data.length_words = EEPROM_SIZE / sizeof(uint32_t),
};

bool nrf5_eeprom_pending(void) { return fds_update || write_pending || gc_pending; }
uint32_t nrf5_eeprom_last_error(void) { return last_fds_error; }
uint32_t nrf5_eeprom_error_count(void) { return fds_error_count; }

void eeprom_update(void) {
    if (!fds_inited || !fds_update || write_pending || gc_pending) {
        return;
    }
    if (fds_update_counter <= FDS_UPDATE_TIMEOUT) {
        fds_update_counter += KEYBOARD_SCAN_INTERVAL;
        return;
    }

    memcpy(write_buffer, buffer, sizeof(write_buffer));
    fds_update = false;
    write_pending = true;
    ret_code_t rc = record_exists ? fds_record_update(&record_desc, &record) : fds_record_write(&record_desc, &record);
    if (rc == FDS_SUCCESS) {
        fds_update_counter = 0;
        return;
    }

    // A full flash page or a busy SDK queue is recoverable. Resetting here
    // reloads the old (usually enabled) RGB settings and looks like an auto-on.
    write_pending = false;
    fds_update = true;
    fds_update_counter = FDS_UPDATE_TIMEOUT + 1;
    last_fds_error = rc;
    ++fds_error_count;
    if (rc == FDS_ERR_NOT_FOUND) {
        record_exists = false;
    } else if (rc == FDS_ERR_NO_SPACE_IN_FLASH) {
        gc_pending = true;
        rc = fds_gc();
        if (rc != FDS_SUCCESS) {
            gc_pending = false;
        }
    }
}

static void fds_evt_handler(fds_evt_t const *event) {
    // FDS errors are SDK error numbers, not indices into a string array.
    NRF_LOG_INFO("FDS event %u result 0x%x", (unsigned)event->id, (unsigned)event->result);
    switch (event->id) {
        case FDS_EVT_INIT:
            fds_init_result = event->result;
            fds_init_complete = true;
            break;
        case FDS_EVT_WRITE:
        case FDS_EVT_UPDATE:
            if (event->write.file_id == FILE_ID && event->write.record_key == RECORD_KEY) {
                write_pending = false;
                last_fds_error = event->result;
                if (event->result == FDS_SUCCESS) {
                    record_exists = true;
                } else {
                    ++fds_error_count;
                    fds_update = true;
                    fds_update_counter = FDS_UPDATE_TIMEOUT + 1;
                }
            }
            break;
        case FDS_EVT_GC:
            gc_pending = false;
            break;
        default:
            break;
    }
}

/**@brief   Sleep until an event is received. */
static void power_manage(void) {
#ifdef SOFTDEVICE_PRESENT
    (void)sd_app_evt_wait();
#else
    __WFE();
#endif
}

static void eeprom_init(void) {
    ret_code_t rc;

    memset(buffer, 0xFF, sizeof(buffer));
    (void)fds_register(fds_evt_handler);
    rc = fds_init();
    APP_ERROR_CHECK(rc);

    while (!fds_init_complete) {
        power_manage();
    }

    APP_ERROR_CHECK(fds_init_result);

    fds_find_token_t ftok = {0};

    memset(&ftok, 0x00, sizeof(fds_find_token_t));
    rc = fds_record_find(FILE_ID, RECORD_KEY, &record_desc, &ftok);

    if (rc == FDS_SUCCESS) {
        record_exists = true;
        fds_flash_record_t flash_record = {0};

        rc = fds_record_open(&record_desc, &flash_record);
        APP_ERROR_CHECK(rc);

        // Existing keyboards have a 36-byte FDS record. Copy only its actual
        // length when growing the store; the remaining bytes stay erased.
        size_t stored_size = (size_t)flash_record.p_header->length_words * sizeof(uint32_t);
        memcpy(buffer, flash_record.p_data, stored_size < sizeof(buffer) ? stored_size : sizeof(buffer));

        rc = fds_record_close(&record_desc);
        APP_ERROR_CHECK(rc);
    } else if (rc == FDS_ERR_NOT_FOUND) {
        // Commit the final initialized QMK settings, rather than first writing
        // an erased record during startup, before the BLE stack is initialized.
        fds_update = true;
    } else {
        APP_ERROR_CHECK(rc);
    }
    fds_inited = true;
}

uint8_t eeprom_read_byte(const uint8_t *addr) {
    if (!fds_inited) {
        eeprom_init();
    }
    uintptr_t offset = (uintptr_t)addr;
    return offset < sizeof(buffer) ? buffer[offset] : 0xFF;
}

void eeprom_write_byte(uint8_t *addr, uint8_t value) {
    if (!fds_inited) {
        eeprom_init();
    }
    uintptr_t offset   = (uintptr_t)addr;
    if (offset >= sizeof(buffer) || buffer[offset] == value) {
        return;
    }
    buffer[offset]     = value;
    fds_update         = true;
    fds_update_counter = 0;
}

uint16_t eeprom_read_word(const uint16_t *addr) {
    const uint8_t *p = (const uint8_t *)addr;
    return eeprom_read_byte(p) | (eeprom_read_byte(p + 1) << 8);
}

uint32_t eeprom_read_dword(const uint32_t *addr) {
    const uint8_t *p = (const uint8_t *)addr;
    return (uint32_t)eeprom_read_byte(p) | ((uint32_t)eeprom_read_byte(p + 1) << 8) | ((uint32_t)eeprom_read_byte(p + 2) << 16) | ((uint32_t)eeprom_read_byte(p + 3) << 24);
}

void eeprom_read_block(void *buf, const void *addr, size_t len) {
    const uint8_t *p    = (const uint8_t *)addr;
    uint8_t *      dest = (uint8_t *)buf;
    while (len--) {
        *dest++ = eeprom_read_byte(p++);
    }
}

void eeprom_write_word(uint16_t *addr, uint16_t value) {
    if (!fds_inited) {
        eeprom_init();
    }
    uintptr_t p        = (uintptr_t)(uint8_t *)addr;
    if (p > sizeof(buffer) - sizeof(value)) {
        return;
    }
    buffer[p++]        = value;
    buffer[p]          = value >> 8;
    fds_update         = true;
    fds_update_counter = 0;
}

void eeprom_write_dword(uint32_t *addr, uint32_t value) {
    if (!fds_inited) {
        eeprom_init();
    }
    uintptr_t p        = (uintptr_t)(uint8_t *)addr;
    if (p > sizeof(buffer) - sizeof(value)) {
        return;
    }
    buffer[p++]        = value;
    buffer[p++]        = value >> 8;
    buffer[p++]        = value >> 16;
    buffer[p]          = value >> 24;
    fds_update         = true;
    fds_update_counter = 0;
}

void eeprom_write_block(const void *buf, void *addr, size_t len) {
    if (!fds_inited) {
        eeprom_init();
    }
    uintptr_t      p   = (uintptr_t)(uint8_t *)addr;
    if (p > sizeof(buffer) || len > sizeof(buffer) - p) {
        return;
    }
    const uint8_t *src = (const uint8_t *)buf;
    while (len--) {
        buffer[p++] = *src++;
    }
    fds_update         = true;
    fds_update_counter = 0;
}

void eeprom_update_byte(uint8_t *addr, uint8_t value) { eeprom_write_byte(addr, value); }

void eeprom_update_word(uint16_t *addr, uint16_t value) { eeprom_write_word(addr, value); }

void eeprom_update_dword(uint32_t *addr, uint32_t value) { eeprom_write_dword(addr, value); }

void eeprom_update_block(const void *buf, void *addr, size_t len) { eeprom_write_block(buf, addr, len); }

// Dummy eeprom

// #define EEPROM_SIZE 32
// static uint8_t buffer[EEPROM_SIZE];

// uint8_t eeprom_read_byte(const uint8_t *addr) {
// 	uintptr_t offset = (uintptr_t)addr;
// 	return buffer[offset];
// }

// void eeprom_write_byte(uint8_t *addr, uint8_t value) {
// 	uintptr_t offset = (uintptr_t)addr;
// 	buffer[offset] = value;
// }

// uint16_t eeprom_read_word(const uint16_t *addr) {
// 	const uint8_t *p = (const uint8_t *)addr;
// 	return eeprom_read_byte(p) | (eeprom_read_byte(p+1) << 8);
// }

// uint32_t eeprom_read_dword(const uint32_t *addr) {
// 	const uint8_t *p = (const uint8_t *)addr;
// 	return eeprom_read_byte(p) | (eeprom_read_byte(p+1) << 8)
// 		| (eeprom_read_byte(p+2) << 16) | (eeprom_read_byte(p+3) << 24);
// }

// void eeprom_read_block(void *buf, const void *addr, uint32_t len) {
// 	const uint8_t *p = (const uint8_t *)addr;
// 	uint8_t *dest = (uint8_t *)buf;
// 	while (len--) {
// 		*dest++ = eeprom_read_byte(p++);
// 	}
// }

// void eeprom_write_word(uint16_t *addr, uint16_t value) {
// 	uint8_t *p = (uint8_t *)addr;
// 	eeprom_write_byte(p++, value);
// 	eeprom_write_byte(p, value >> 8);
// }

// void eeprom_write_dword(uint32_t *addr, uint32_t value) {
// 	uint8_t *p = (uint8_t *)addr;
// 	eeprom_write_byte(p++, value);
// 	eeprom_write_byte(p++, value >> 8);
// 	eeprom_write_byte(p++, value >> 16);
// 	eeprom_write_byte(p, value >> 24);
// }

// void eeprom_write_block(const void *buf, void *addr, uint32_t len) {
// 	uint8_t *p = (uint8_t *)addr;
// 	const uint8_t *src = (const uint8_t *)buf;
// 	while (len--) {
// 		eeprom_write_byte(p++, *src++);
// 	}
// }

// void eeprom_update_byte(uint8_t *addr, uint8_t value) {
// 	eeprom_write_byte(addr, value);
// }

// void eeprom_update_word(uint16_t *addr, uint16_t value) {
// 	uint8_t *p = (uint8_t *)addr;
// 	eeprom_write_byte(p++, value);
// 	eeprom_write_byte(p, value >> 8);
// }

// void eeprom_update_dword(uint32_t *addr, uint32_t value) {
// 	uint8_t *p = (uint8_t *)addr;
// 	eeprom_write_byte(p++, value);
// 	eeprom_write_byte(p++, value >> 8);
// 	eeprom_write_byte(p++, value >> 16);
// 	eeprom_write_byte(p, value >> 24);
// }

// void eeprom_update_block(const void *buf, void *addr, uint32_t len) {
// 	uint8_t *p = (uint8_t *)addr;
// 	const uint8_t *src = (const uint8_t *)buf;
// 	while (len--) {
// 		eeprom_write_byte(p++, *src++);
// 	}
// }
