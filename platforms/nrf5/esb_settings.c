// SPDX-License-Identifier: GPL-2.0-or-later
#include "esb_settings.h"
#include "fds.h"
#include "../eeprom.h"
#ifdef NRF5_ESB_DONGLE
#    include "app_usbd.h"
#    include "nrf_soc.h"
static volatile bool init_complete;
static volatile uint32_t init_result;
#endif
#include <string.h>

#define ESB_FILE 0x1002
#define ESB_RECORD 0x1001
#define ESB_MAGIC 0x45534232u
_Static_assert(sizeof(nrf5_esb_settings_t) == 32, "ESB settings layout changed");
static nrf5_esb_settings_t settings, writing;
static fds_record_desc_t descriptor;
static bool exists, dirty, busy, collecting, registered;
static uint32_t error;

static uint32_t checksum(const nrf5_esb_settings_t *s) {
    const uint8_t *p = (const uint8_t *)s;
    uint32_t crc = 0xFFFFFFFF;
    for (uint8_t i = 0; i < 28; i++) {
        crc ^= p[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
    }
    return ~crc;
}
static bool valid_key(const uint8_t key[16]) {
    uint8_t any = 0, not_ff = 0;
    for (uint8_t i = 0; i < 16; i++) { any |= key[i]; not_ff |= key[i] ^ 255; }
    return any && not_ff;
}
static void event(const fds_evt_t *e) {
#ifdef NRF5_ESB_DONGLE
    if (e->id == FDS_EVT_INIT) { init_result = e->result; init_complete = true; }
#endif
    if ((e->id == FDS_EVT_WRITE || e->id == FDS_EVT_UPDATE) && e->write.file_id == ESB_FILE && e->write.record_key == ESB_RECORD) {
        busy = false; error = e->result;
        if (!e->result) exists = true;
        else dirty = true;
        if (!dirty) memset(&writing, 0, sizeof(writing));
    } else if (e->id == FDS_EVT_GC) {
        collecting = false;
        if (e->result) error = e->result;
    }
}
void nrf5_esb_settings_init(void) {
    // Ensure the existing EEPROM owner has initialized FDS; use a separate
    // file/record instead of consuming VIA macro or OLED EEPROM addresses.
#ifndef NRF5_ESB_DONGLE
    (void)eeprom_read_byte((const uint8_t *)0);
#endif
    settings.magic = ESB_MAGIC;
    uint32_t rc = fds_register(event);
    if (rc) { error = rc; return; }
    registered = true;
#ifdef NRF5_ESB_DONGLE
    rc = fds_init();
    if (rc) { error = rc; registered = false; return; }
    while (!init_complete) {
        while (app_usbd_event_queue_process()) {}
        (void)sd_app_evt_wait();
    }
    if (init_result) { error = init_result; registered = false; return; }
#endif
    fds_find_token_t token = {0};
    rc = fds_record_find(ESB_FILE, ESB_RECORD, &descriptor, &token);
    if (rc == FDS_ERR_NOT_FOUND) return;
    if (rc) { error = rc; return; }
    exists = true;
    fds_flash_record_t record = {0};
    rc = fds_record_open(&descriptor, &record);
    if (rc) { error = rc; return; }
    nrf5_esb_settings_t stored;
    bool valid = record.p_header->length_words == sizeof(stored) / 4;
    if (valid) memcpy(&stored, record.p_data, sizeof(stored));
    (void)fds_record_close(&descriptor);
    if (valid && stored.magic == ESB_MAGIC && stored.crc == checksum(&stored) && stored.mode <= NRF5_MODE_NONE &&
        stored.paired <= 1 && (!stored.paired || (stored.address && valid_key(stored.key)))) settings = stored;
}
void nrf5_esb_settings_task(void) {
    if (!registered || !dirty || busy || collecting) return;
    settings.crc = checksum(&settings); writing = settings;
    fds_record_t record = {.file_id = ESB_FILE, .key = ESB_RECORD,
        .data = {.p_data = &writing, .length_words = sizeof(writing) / 4}};
    busy = true; dirty = false;
    uint32_t rc = exists ? fds_record_update(&descriptor, &record) : fds_record_write(&descriptor, &record);
    if (!rc) return;
    busy = false; dirty = true; error = rc;
    if (rc == FDS_ERR_NOT_FOUND) exists = false;
    if (rc == FDS_ERR_NO_SPACE_IN_FLASH) {
        collecting = true;
        rc = fds_gc();
        if (rc) { collecting = false; error = rc; }
    }
}
const nrf5_esb_settings_t *nrf5_esb_settings(void) { return &settings; }
bool nrf5_esb_settings_pair(const uint8_t key[16], uint32_t address) {
    if (!registered || !address || !valid_key(key)) return false;
    memcpy(settings.key, key, 16); settings.address = address; settings.paired = 1;
    dirty = true; error = 0; return true;
}
void nrf5_esb_settings_clear(void) {
    if (!registered) return;
    memset(settings.key, 0, 16); settings.address = 0; settings.paired = 0;
    if (settings.mode == NRF5_MODE_ESB) settings.mode = NRF5_MODE_AUTO;
    dirty = true; error = 0;
}
bool nrf5_esb_settings_mode(uint8_t mode) {
    if (!registered || mode > NRF5_MODE_NONE) return false;
    if (settings.mode != mode) { settings.mode = mode; dirty = true; error = 0; }
    return true;
}
bool nrf5_esb_settings_pending(void) { return dirty || busy || collecting; }
uint32_t nrf5_esb_settings_error(void) { return error; }
