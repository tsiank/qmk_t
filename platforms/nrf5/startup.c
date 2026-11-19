// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdint.h>
#include <string.h>

#include "nrf.h"

extern uint32_t __bss_start__;
extern uint32_t __bss_end__;
extern void     __libc_init_array(void);
extern int      main(void);

// With -nostartfiles, newlib's constructor runner still needs this hook.
void _init(void) {}

// Nordic's Reset_Handler copies .data and calls SystemInit before entering
// _start. Do not use the toolchain's crt0: some ARM toolchains ship a crt0
// which issues semihosting traps before main, even when linking libnosys.
__attribute__((noreturn)) void _start(void) {
    memset(&__bss_start__, 0, (uintptr_t)&__bss_end__ - (uintptr_t)&__bss_start__);
    __libc_init_array();
    main();

    while (1) {
        __WFE();
    }
}
