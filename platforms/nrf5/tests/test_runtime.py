# Copyright 2026 QMK Contributors
# SPDX-License-Identifier: GPL-2.0-or-later

"""Native regression checks for nRF5 storage migration and dual-mode routing.

Run: python3 -m unittest discover -s platforms/nrf5/tests -v
Only Nordic hardware calls are stubbed; the production C sources are compiled.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which('cc')
        if not cls.cc:
            raise unittest.SkipTest('Native C compiler is required')

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)

    def write(self, name, content):
        (self.work / name).write_text(content)

    def compile_and_run(self, source, flags=(), args=()):
        executable = self.work / 'test'
        subprocess.run([
            self.cc, '-std=c11', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-fno-pie', '-no-pie', '-I', str(self.work), *flags,
            str(ROOT / source), str(self.work / 'test.c'), '-o', str(executable),
        ], check=True, capture_output=True, text=True)
        subprocess.run([str(executable), *args], check=True, capture_output=True, text=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1'})

    def check_eeprom(self, mode, capacity=64):
        self.write('nrf_log.h', '#define NRF_LOG_INFO(...)\n')
        self.write('nvm_eeprom_eeconfig_internal.h', '#define STATIC_ASSERT _Static_assert\n#define EECONFIG_SIZE 37\n')
        self.write('fds.h', r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#define __ALIGN(n) __attribute__((aligned(n)))
#define __WFE() ((void)0)
#define APP_ERROR_CHECK(code) do { if ((code) != 0) abort(); } while (0)
typedef int ret_code_t;
enum { FDS_SUCCESS, FDS_ERR_NOT_FOUND, FDS_ERR_NO_SPACE_IN_FLASH, FDS_ERR_BUSY };
enum { FDS_EVT_INIT, FDS_EVT_WRITE, FDS_EVT_UPDATE, FDS_EVT_DEL_RECORD, FDS_EVT_DEL_FILE, FDS_EVT_GC };
typedef struct { int unused; } fds_record_desc_t;
typedef struct { int unused; } fds_find_token_t;
typedef struct { unsigned length_words; } fds_header_t;
typedef struct { const fds_header_t *p_header; const void *p_data; } fds_flash_record_t;
typedef struct {
    unsigned file_id, key;
    struct { const void *p_data; unsigned length_words; } data;
} fds_record_t;
typedef struct { unsigned record_id, file_id, record_key; } fds_event_record_t;
typedef struct { int id, result; fds_event_record_t write, del; } fds_evt_t;
typedef struct { unsigned valid_records, dirty_records; } fds_stat_t;
int fds_register(void (*handler)(const fds_evt_t *));
int fds_init(void);
int fds_stat(fds_stat_t *stat);
int fds_gc(void);
int fds_record_find(unsigned file, unsigned key, fds_record_desc_t *desc, fds_find_token_t *token);
int fds_record_open(fds_record_desc_t *desc, fds_flash_record_t *record);
int fds_record_close(fds_record_desc_t *desc);
int fds_record_write(fds_record_desc_t *desc, const fds_record_t *record);
int fds_record_update(fds_record_desc_t *desc, const fds_record_t *record);
''')
        self.write('test.c', r'''
#include <assert.h>
#include <string.h>
#include "fds.h"
#include "eeprom.h"
#include "eeprom_nrf.h"
static void (*callback)(const fds_evt_t *);
static uint8_t *stored;
static fds_header_t header;
static uint8_t written[EEPROM_SIZE];
static unsigned writes, updates, collections;
static bool missing, full_once, busy_once, snapshot;
static const uint8_t *held;
int fds_register(void (*handler)(const fds_evt_t *)) { callback = handler; return 0; }
int fds_init(void) { fds_evt_t event = {.id = FDS_EVT_INIT}; callback(&event); return 0; }
int fds_stat(fds_stat_t *stat) { memset(stat, 0, sizeof(*stat)); return 0; }
int fds_gc(void) {
    collections++;
    fds_evt_t event = {.id = FDS_EVT_GC}; callback(&event); return 0;
}
int fds_record_find(unsigned file, unsigned key, fds_record_desc_t *desc, fds_find_token_t *token) {
    assert(file == 0x1000 && key == 0x1001);
    return missing ? FDS_ERR_NOT_FOUND : FDS_SUCCESS;
}
int fds_record_open(fds_record_desc_t *desc, fds_flash_record_t *record) {
    record->p_header = &header; record->p_data = stored; return 0;
}
int fds_record_close(fds_record_desc_t *desc) { return 0; }
int fds_record_write(fds_record_desc_t *desc, const fds_record_t *record) {
    assert(record->data.length_words * 4 == EEPROM_SIZE);
    memcpy(written, record->data.p_data, EEPROM_SIZE); writes++;
    held = record->data.p_data;
    fds_evt_t event = {.id = FDS_EVT_WRITE, .write = {.file_id = 0x1000, .record_key = 0x1001}};
    if (!snapshot) callback(&event);
    return 0;
}
int fds_record_update(fds_record_desc_t *desc, const fds_record_t *record) {
    updates++;
    if (full_once && updates == 1) return FDS_ERR_NO_SPACE_IN_FLASH;
    if (busy_once && updates <= 3) return FDS_ERR_BUSY;
    return fds_record_write(desc, record);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    missing = strcmp(argv[1], "missing") == 0;
    full_once = strcmp(argv[1], "gc") == 0;
    busy_once = strcmp(argv[1], "busy") == 0;
    snapshot = strcmp(argv[1], "snapshot") == 0;
    bool large = strcmp(argv[1], "large") == 0;
    size_t size = large ? 2048 : 36;
    stored = malloc(size);
    memset(stored, 0x55, size);
    header.length_words = size / 4;
    assert(eeprom_read_byte((void *)0) == (missing ? 0xFF : 0x55));
    assert(eeprom_read_byte((void *)36) == (!missing && large ? 0x55 : 0xFF));
    assert(eeprom_read_byte((void *)(EEPROM_SIZE - 1)) == (!missing && large ? 0x55 : 0xFF));
    assert(eeprom_read_byte((void *)EEPROM_SIZE) == 0xFF);
    eeprom_write_dword((void *)16, 0xFEDCBA98);
    assert(eeprom_read_dword((void *)16) == 0xFEDCBA98);
    eeprom_write_byte((void *)(EEPROM_SIZE - 1), 0x12);
    eeprom_write_word((void *)(EEPROM_SIZE - 1), 0x5678);
    eeprom_write_dword((void *)(EEPROM_SIZE - 2), 0xDEADBEEF);
    uint8_t block[4] = {1, 2, 3, 4};
    eeprom_write_block(block, (void *)(EEPROM_SIZE - 2), sizeof(block));
    assert(eeprom_read_byte((void *)(EEPROM_SIZE - 1)) == 0x12);
    eeprom_write_byte((void *)EEPROM_SIZE, 0);
    eeprom_write_block(block, (void *)(EEPROM_SIZE - 4), sizeof(block));
    uint8_t actual[4];
    eeprom_read_block(actual, (void *)(EEPROM_SIZE - 4), sizeof(actual));
    assert(memcmp(actual, block, sizeof(block)) == 0);
    for (int i = 0; i < 100; i++) eeprom_update();
    assert(updates == (missing ? 0 : full_once ? 2 : busy_once ? 4 : 1));
    assert(writes == 1);
    assert(collections == (full_once ? 1 : 0));
    assert(written[16] == 0x98 && written[19] == 0xFE);
    if (snapshot) {
        eeprom_write_byte((void *)16, 0x42);
        assert(held[16] == 0x98); // Cache writes must not mutate the SDK's pending source.
        fds_evt_t done = {.id = FDS_EVT_UPDATE, .write = {.file_id = 0x1000, .record_key = 0x1001}};
        callback(&done);
        snapshot = false;
        for (int i = 0; i < 100; i++) eeprom_update();
        assert(updates == 2 && writes == 2 && written[16] == 0x42);
    }
    free(stored);
    return 0;
}
''')
        self.compile_and_run('platforms/nrf5/eeprom.c', flags=(
            '-DEEPROM_NRF', f'-DEEPROM_SIZE={capacity}', '-DKEYBOARD_SCAN_INTERVAL=6', '-I', str(ROOT / 'platforms'),
            '-I', str(ROOT / 'platforms/nrf5'),
        ), args=(mode,))

    def test_via_two_kilobyte_store_migration(self):
        self.check_eeprom('legacy', capacity=2048)

    def test_via_two_kilobyte_store_async_snapshot(self):
        self.check_eeprom('snapshot', capacity=2048)

    def test_legacy_record_migration(self):
        self.check_eeprom('legacy')

    def test_ui_capacity_preserves_old_bytes_and_snapshots_large_writes(self):
        self.check_eeprom('legacy', capacity=3840)
        self.check_eeprom('snapshot', capacity=3840)

    def test_larger_record_is_clipped_to_capacity(self):
        self.check_eeprom('large')

    def test_first_boot_and_storage_bounds(self):
        self.check_eeprom('missing')

    def test_full_flash_collects_and_retries_without_reset(self):
        self.check_eeprom('gc')

    def test_busy_flash_queue_retries_without_reset(self):
        self.check_eeprom('busy')

    def test_in_flight_flash_uses_an_immutable_snapshot(self):
        self.check_eeprom('snapshot')

    def check_routing(self, nkro):
        self.write('action.h', '#pragma once\n#include <stdbool.h>\ntypedef struct { struct { bool pressed; } event; } keyrecord_t;\n')
        self.write('host.h', '''
#pragma once
#include <stdbool.h>
typedef struct { int unused; } host_driver_t;
host_driver_t *host_get_driver(void);
void host_set_driver(host_driver_t *driver);
bool host_can_send_nkro(void);
''')
        self.write('nrf5_usb.h', '#include "host.h"\nextern unsigned char keyboard_protocol;\nhost_driver_t *nrf5_usb_driver(void);\n')
        self.write('ble_service.h', '#include "host.h"\nhost_driver_t *nrf5_ble_driver(void);\n')
        self.write('ble_hids_service.h', '#include <stdbool.h>\nbool nrf5_ble_can_send_nkro(void);\n')
        self.write('test.c', r'''
#include <assert.h>
#include "host.h"
#include "outputselect.h"
#include "keycodes.h"
static host_driver_t usb, ble, other, *active;
static bool ble_nkro;
unsigned char keyboard_protocol = 1;
host_driver_t *host_get_driver(void) { return active; }
void host_set_driver(host_driver_t *driver) { active = driver; }
host_driver_t *nrf5_usb_driver(void) { return &usb; }
host_driver_t *nrf5_ble_driver(void) { return &ble; }
bool nrf5_ble_can_send_nkro(void) { return ble_nkro; }
int main(void) {
#ifdef NKRO_ENABLE
    const bool enabled = true;
#else
    const bool enabled = false;
#endif
    assert(!host_can_send_nkro());
    set_output(OUTPUT_USB);
    assert(host_can_send_nkro() == enabled);
    keyboard_protocol = 0;
    assert(!host_can_send_nkro());
    keyboard_protocol = 1;
    set_output(OUTPUT_BLUETOOTH);
    assert(!host_can_send_nkro());
    ble_nkro = true;
    assert(host_can_send_nkro() == enabled);
    set_output(OUTPUT_NONE);
    assert(!host_can_send_nkro());
    host_set_driver(&other);
    assert(!host_can_send_nkro());
    keyrecord_t record = {.event.pressed = true};
    assert(!process_nrf5_output(OU_USB, &record));
    assert(host_get_driver() == &usb);
    record.event.pressed = false;
    assert(!process_nrf5_output(OU_BT, &record));
    assert(host_get_driver() == &usb);
    record.event.pressed = true;
    assert(!process_nrf5_output(OU_BT, &record));
    assert(host_get_driver() == &ble);
    assert(!process_nrf5_output(QK_OUTPUT_NONE, &record));
    assert(host_get_driver() == 0);
    assert(process_nrf5_output(KC_A, &record));
    return 0;
}
''')
        self.compile_and_run('tmk_core/protocol/nrf5/outputselect.c', flags=(
            '-I', str(ROOT / 'tmk_core/protocol/nrf5'), '-I', str(ROOT / 'quantum'),
            *(['-DNKRO_ENABLE'] if nkro else []),
        ))

    def test_nkro_tracks_active_transport_and_boot_protocol(self):
        self.check_routing(True)

    def test_nkro_disabled(self):
        self.check_routing(False)
