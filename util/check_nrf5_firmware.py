#!/usr/bin/env python3
# Copyright 2026 QMK Contributors
# SPDX-License-Identifier: GPL-2.0-or-later

"""Check nRF52840 application vectors and reject debugger-dependent firmware.

Run after building: python3 util/check_nrf5_firmware.py .build/ciank67_default.elf
This checks the linked image, not whether it works on physical hardware.
"""

import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def check_firmware(elf, prefix, application_address):
    def run(tool, *args):
        return subprocess.check_output([prefix + tool, *map(str, args)], text=True)

    symbols = {}
    for line in run('nm', '-n', elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)

    for name in ('__isr_vector', '__StackTop', 'Reset_Handler', '_start', 'main'):
        if name not in symbols:
            raise ValueError(f'Missing startup symbol: {name}')

    if symbols['__isr_vector'] != application_address:
        raise ValueError(f'Application vectors must start at {application_address:#x}')

    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / 'text.bin'
        run('objcopy', '-O', 'binary', '--only-section=.text', elf, binary)
        stack, reset = struct.unpack_from('<II', binary.read_bytes())

    if stack != symbols['__StackTop'] or not 0x20000000 < stack <= 0x20040000:
        raise ValueError(f'Invalid initial stack pointer: {stack:#x}')
    if reset != symbols['Reset_Handler'] | 1:
        raise ValueError(f'Invalid reset vector: {reset:#x}')
    if not application_address <= reset < 0x100000:
        raise ValueError('Reset handler is outside application flash')

    disassembly = run('objdump', '-d', elf)
    if re.search(r'\bbkpt\s+(?:0x0*ab|171)\b', disassembly, re.IGNORECASE):
        raise ValueError('Semihosting BKPT 0xAB found; this image requires a debugger')
    if '_mainCRTStartup' in symbols or 'initialise_monitor_handles' in symbols:
        raise ValueError('Toolchain monitor startup linked instead of Nordic startup')

    print(f'{elf}: vectors OK, stack {stack:#x}, reset {reset:#x}, no semihosting traps')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--prefix', default='arm-none-eabi-', help='Toolchain prefix, optionally including its directory')
    parser.add_argument('--application-address', type=lambda value: int(value, 0), default=0x26000)
    args = parser.parse_args()
    try:
        check_firmware(args.elf, args.prefix, args.application_address)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Firmware check failed: {error}\n')


if __name__ == '__main__':
    main()
