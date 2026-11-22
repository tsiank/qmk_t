// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "nrf.h"
#include "app_error.h"
#include "app_util.h"
#include "nrf_fstorage.h"
#include <string.h>
NRF_SECTION_DEF(fs_data, nrf_fstorage_t);

typedef struct { uint32_t magic, id, pc, info, code; } ui_fault_t;
static volatile ui_fault_t fault __attribute__((section(".nrf5_fault"), used));
#define UI_FAULT_MAGIC 0x55494631u

// Retain the last fatal error across a software reset, without attempting
// another flash write or USB/I2C operation from the fault handler.
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
    __disable_irq();
    fault.magic = 0; fault.id = id; fault.pc = pc; fault.info = info; fault.code = 0;
    if (id == NRF_FAULT_ID_SDK_ERROR && info >= 0x20000000u && info <= 0x20040000u - sizeof(error_info_t))
        fault.code = ((const error_info_t *)(uintptr_t)info)->err_code;
    __DMB(); fault.magic = UI_FAULT_MAGIC; __DMB();
    NVIC_SystemReset();
}
static void put32(uint8_t *p, uint32_t value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
void nrf5_ui_diagnostics(uint8_t *data) {
    memset(data + 1, 0, 31); data[1] = 1;
    put32(data + 2, timer_read32());
    if (fault.magic == UI_FAULT_MAGIC) {
        put32(data + 6, fault.id); put32(data + 10, fault.pc);
        put32(data + 14, fault.info); put32(data + 18, fault.code);
    }
    if (NRF_FSTORAGE_INSTANCE_CNT) {
        // Linker section boundaries are addresses, not C-sized objects.
        volatile uintptr_t address = (uintptr_t)NRF_FSTORAGE_INSTANCE_GET(0);
        put32(data + 22, ((const nrf_fstorage_t *)address)->start_addr);
    }
    volatile uintptr_t mbr_address = MBR_BOOTLOADER_ADDR;
    uint32_t bootloader = *(const volatile uint32_t *)mbr_address;
    if (bootloader == 0xffffffffu) bootloader = *MBR_UICR_BOOTLOADER_ADDR;
    put32(data + 26, bootloader);
    data[30] = NRF_POWER->RESETREAS >> 8; data[31] = NRF_POWER->RESETREAS;
}
