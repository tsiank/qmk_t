// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lighting.h"

#include "process_underglow.h"

#if defined(PROTOCOL_NRF5) && defined(SEND_STRING_ENABLE)
#    include <stdio.h>
#    include "eeprom_nrf.h"
#    include "nrf_gpio.h"
#    ifdef RGB_MATRIX_IS31FL3741
#        include "nrf5/i2c_master.h"
#    endif
static char report_buffer[160];
static uint8_t report_position;
static bool report_pending;
static uint16_t last_report_character;
#endif

void ciank67_lighting_report(void) {
#if defined(PROTOCOL_NRF5) && defined(SEND_STRING_ENABLE)
    if (report_pending) return;
    unsigned matrix = 0, under = 0, matrix_mode = 0, under_mode = 0, power = 0, flags = 0;
    unsigned long errors = 0;
#    ifdef RGB_MATRIX_ENABLE
    matrix = rgb_matrix_is_enabled();
    matrix_mode = rgb_matrix_get_mode();
    flags = rgb_matrix_get_flags();
#    endif
#    ifdef RGBLIGHT_ENABLE
    under = rgblight_is_enabled();
    under_mode = rgblight_get_mode();
#    endif
#    ifdef RGB_PWR_PIN
    power = nrf_gpio_pin_read(RGB_PWR_PIN);
#    endif
#    ifdef RGB_MATRIX_IS31FL3741
    errors = nrf5_i2c_error_count();
#    endif
    snprintf(report_buffer, sizeof(report_buffer),
             "ciank67-light-music-color-picker-r15 t=%lu m=%u:%u f=%u u=%u:%u p=%u twi=%lu ee=%lx/%lu irq=%lu reset=%lx\n",
             (unsigned long)timer_read32(), matrix, matrix_mode, flags, under, under_mode, power, errors,
             (unsigned long)nrf5_eeprom_last_error(), (unsigned long)nrf5_eeprom_error_count(),
             (unsigned long)__get_IPSR(), (unsigned long)NRF_POWER->RESETREAS);
    report_position = 0;
    report_pending = true;
#endif
}

void ciank67_lighting_task(void) {
#if defined(PROTOCOL_NRF5) && defined(SEND_STRING_ENABLE)
    if (!report_pending || timer_elapsed(last_report_character) < 40) return;
    // Let the USB/BLE event loop drain reports between characters. A synchronous
    // send_string of the entire status line would overflow the Nordic HID queue.
    char character[2] = {report_buffer[report_position++], '\0'};
    send_string(character);
    last_report_character = timer_read();
    if (!report_buffer[report_position]) report_pending = false;
#endif
}

static bool matrix_enabled(void) {
#ifdef RGB_MATRIX_ENABLE
    return rgb_matrix_is_enabled();
#else
    return false;
#endif
}

static bool underglow_enabled(void) {
#ifdef RGBLIGHT_ENABLE
    return rgblight_is_enabled();
#else
    return false;
#endif
}

static void set_matrix_enabled(bool enabled) {
#ifdef RGB_MATRIX_ENABLE
    if (enabled) {
        rgb_matrix_enable();
    } else {
        rgb_matrix_disable();
    }
#endif
}

static void set_underglow_enabled(bool enabled) {
#ifdef RGBLIGHT_ENABLE
    enabled ? rgblight_enable() : rgblight_disable();
#endif
}

bool ciank67_process_lighting(uint16_t keycode, keyrecord_t *record, uint16_t combined_toggle) {
    // stable processes RGB controls on key up to avoid layer/key-release edge
    // cases. Keep that behavior and consume both edges. Leave I2C running so
    // the matrix state machine can actually transmit its all-black off frame.
    if (keycode == combined_toggle) {
        if (!record->event.pressed) {
            bool enabled = !(matrix_enabled() || underglow_enabled());
            set_matrix_enabled(enabled);
            set_underglow_enabled(enabled);
        }
        return false;
    }

    switch (keycode) {
#ifdef RGB_MATRIX_ENABLE
        case QK_RGB_MATRIX_TOGGLE:
        case QK_RGB_MATRIX_ON:
        case QK_RGB_MATRIX_OFF:
            if (!record->event.pressed) {
                set_matrix_enabled(keycode == QK_RGB_MATRIX_ON || (keycode == QK_RGB_MATRIX_TOGGLE && !matrix_enabled()));
            }
            return false;
#endif
#ifdef RGBLIGHT_ENABLE
        case QK_UNDERGLOW_TOGGLE:
            if (!record->event.pressed) {
                set_underglow_enabled(!underglow_enabled());
            }
            return false;
#endif
    }

#ifdef RGBLIGHT_ENABLE
    // The newer underglow processor acts on key down. Adapt its controls to
    // stable's key-up convention, rather than running it a second time later.
    if (keycode >= QK_UNDERGLOW_MODE_NEXT && keycode <= QK_UNDERGLOW_SPEED_DOWN) {
        if (!record->event.pressed) {
            keyrecord_t released = *record;
            released.event.pressed = true;
            process_underglow(keycode, &released);
        }
        return false;
    }
    // These legacy direct-mode keys remain in QMK's keycode enum, but are no
    // longer handled by process_underglow(). Preserve the existing keymap.
    uint8_t mode;
    uint8_t family_end = 0;
    switch (keycode) {
        case RGB_MODE_PLAIN: mode = RGBLIGHT_MODE_STATIC_LIGHT; break;
#    ifdef RGBLIGHT_EFFECT_BREATHING
        case RGB_MODE_BREATHE: mode = RGBLIGHT_MODE_BREATHING; family_end = RGBLIGHT_MODE_BREATHING_end; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_RAINBOW_MOOD
        case RGB_MODE_RAINBOW: mode = RGBLIGHT_MODE_RAINBOW_MOOD; family_end = RGBLIGHT_MODE_RAINBOW_MOOD_end; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_RAINBOW_SWIRL
        case RGB_MODE_SWIRL: mode = RGBLIGHT_MODE_RAINBOW_SWIRL; family_end = RGBLIGHT_MODE_RAINBOW_SWIRL_end; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_SNAKE
        case RGB_MODE_SNAKE: mode = RGBLIGHT_MODE_SNAKE; family_end = RGBLIGHT_MODE_SNAKE_end; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_KNIGHT
        case RGB_MODE_KNIGHT: mode = RGBLIGHT_MODE_KNIGHT; family_end = RGBLIGHT_MODE_KNIGHT_end; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_CHRISTMAS
        case RGB_MODE_XMAS: mode = RGBLIGHT_MODE_CHRISTMAS; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_STATIC_GRADIENT
        case RGB_MODE_GRADIENT: mode = RGBLIGHT_MODE_STATIC_GRADIENT; family_end = RGBLIGHT_MODE_STATIC_GRADIENT_end; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_RGB_TEST
        case RGB_MODE_RGBTEST: mode = RGBLIGHT_MODE_RGB_TEST; break;
#    endif
#    ifdef RGBLIGHT_EFFECT_TWINKLE
        case RGB_MODE_TWINKLE: mode = RGBLIGHT_MODE_TWINKLE; family_end = RGBLIGHT_MODE_TWINKLE_end; break;
#    endif
        default: return true;
    }
    if (!record->event.pressed) {
        uint8_t current = rgblight_get_mode();
        if (mode <= current && current < family_end) {
            rgblight_step();
        } else {
            rgblight_mode(mode);
        }
    }
    return false;
#else
    return true;
#endif
}
