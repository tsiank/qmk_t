// SPDX-License-Identifier: GPL-2.0-or-later
#include "ciank_pad.h"
#include "nrf5_ui.h"
#include "nrf5_lighting.h"
#include "nrf5_midi_typing.h"
#include "ble_service.h"
#include "adc.h"
#include "outputselect.h"
#include <stdio.h>

#ifdef VIA_ENABLE
#    include "via.h"

void via_init_kb(void) {
    uint8_t stored_id = 0;
    const uint8_t keymap_id = CIANK_PAD_KEYMAP_ID;
    via_read_custom_config(&stored_id, 0, sizeof(stored_id));
    if (stored_id != keymap_id) {
        // QMK's VIA magic contains only the build date. A same-day switch
        // between default and test would otherwise keep the previous map.
        via_eeprom_set_valid(false);
        via_update_custom_config(&keymap_id, 0, sizeof(keymap_id));
    }
}
#endif

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        reset_power_save_counter();
#ifdef OLED_ENABLE
        oled_on();
#endif
    }
    if (!nrf5_process_midi_typing(keycode, record, MIDI_TYPING_TOGGLE)) return false;
    if (!nrf5_process_lighting(keycode, record, RGBG_TOG)) return false;
    switch (keycode) {
        case DISC: if (record->event.pressed) ble_disconnect(); return false;
        case ADVW: if (record->event.pressed) advertising_without_whitelist(); return false;
        case ADVS: if (record->event.pressed) advertising_start(false); return false;
        case SEL0: case SEL1: case SEL2:
            if (record->event.pressed) restart_advertising_id(keycode - SEL0);
            return false;
        case DELB: if (record->event.pressed) advertising_start(true); return false;
        case REBOOT: if (record->event.pressed) NVIC_SystemReset(); return false;
        case ENT_DFU:
            if (record->event.pressed) {
                // Preserve the original Adafruit/UF2 entry code. QK_BOOT is
                // available separately for the Nordic DFU bootloader.
                sd_power_gpregret_clr(0, 0xffffffff);
                sd_power_gpregret_set(0, 0x57);
                NVIC_SystemReset();
            }
            return false;
        case SLEEP:
            if (!record->event.pressed) deep_sleep_mode_enter();
            return false;
        case RGBRST:
            if (!record->event.pressed) { eeconfig_update_rgblight_default(); rgblight_enable(); }
            return false;
        case VOLUP:
            record->event.pressed ? register_code(KC_VOLU) : unregister_code(KC_VOLU);
            return false;
        case LIGHT_STATUS: if (!record->event.pressed) nrf5_lighting_report(); return false;
        case UNDERGLOW_TEST: if (!record->event.pressed) nrf5_underglow_test(); return false;
        case BATT_LV: case BATT_LV2:
            if (record->event.pressed) {
                char text[16];
                if (keycode == BATT_LV) snprintf(text, sizeof(text), "%umV", (unsigned)get_vcc());
                else snprintf(text, sizeof(text), "%u%%", (unsigned)get_battery_level());
                send_string(text);
            }
            return false;
    }
    return process_record_user(keycode, record);
}

#ifdef NRF5_ENCODER_ENABLE
bool encoder_update_kb(uint8_t index, bool clockwise) { return nrf5_encoder_update(index, clockwise); }
#endif
#ifdef NRF5_OLED_STATUS_ENABLE
bool oled_task_kb(void) { return nrf5_oled_task(); }
#endif
