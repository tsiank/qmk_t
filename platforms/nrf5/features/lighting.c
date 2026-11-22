// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_lighting.h"

#include "process_underglow.h"
#ifdef NRF5_LIGHTING_DIAGNOSTICS
#    include "adc.h"
#    include "nrf_gpio.h"
#    include <string.h>
static uint16_t lighting_presses, lighting_releases, lighting_controls, lighting_last_key;
static uint8_t lighting_last_edge, lighting_last_mask, lighting_last_values;
void nrf5_lighting_control_trace(uint8_t mask, uint8_t values) {
    if (!mask) return;
    ++lighting_controls;
    lighting_last_mask = mask;
    lighting_last_values = values;
}
static void lighting_put16(uint8_t *p, uint16_t value) { p[0] = value >> 8; p[1] = value; }
void nrf5_lighting_diagnostics(uint8_t data[32]) {
    memset(data + 1, 0, 31);
    data[1] = 1;
#    ifdef RGB_MATRIX_ENABLE
    data[2] |= rgb_matrix_is_enabled() ? 1 : 0;
    data[20] = rgb_matrix_get_mode();
    data[21] = rgb_matrix_get_val();
#    endif
#    ifdef RGBLIGHT_ENABLE
    data[2] |= rgblight_is_enabled() ? 2 : 0;
    data[22] = rgblight_get_mode();
    data[23] = rgblight_get_val();
#    endif
#    ifdef RGB_PWR_PIN
    data[3] = nrf_gpio_pin_out_read(RGB_PWR_PIN);
#    endif
    uint32_t now = timer_read32();
    for (unsigned i = 0; i < 4; ++i) data[4 + i] = now >> (24 - 8 * i);
    lighting_put16(data + 8, get_vcc());
    lighting_put16(data + 10, lighting_presses);
    lighting_put16(data + 12, lighting_releases);
    lighting_put16(data + 14, lighting_controls);
    lighting_put16(data + 16, lighting_last_key);
    data[18] = lighting_last_edge;
    data[19] = lighting_last_mask;
    data[24] = lighting_last_values;
}
#endif
#ifdef NRF5_ESB_TX
#    include "nrf5_esb.h"
extern volatile uint8_t nrf5_ble_led_last_report, nrf5_ble_led_report_count;
void nrf5_esb_led_diagnostics(uint8_t data[3]) {
    data[0] = host_keyboard_leds() & 31;
#    ifdef RGBLIGHT_LAYERS
    if (rgblight_get_layer_state(0)) data[0] |= 0x80;
#    endif
    data[1] = nrf5_ble_led_last_report;
    data[2] = nrf5_ble_led_report_count;
}
#endif
#ifdef OLED_ENABLE
#    include "oled_driver.h"
#endif

#if defined(PROTOCOL_NRF5) && defined(SEND_STRING_ENABLE)
#    include <stdio.h>
#    include "eeprom_nrf.h"
#    include "nrf_gpio.h"
#    if defined(RGB_MATRIX_IS31FL3741) || defined(OLED_ENABLE)
#        include "nrf5/i2c_master.h"
#    endif
#    ifdef MIDI_ENABLE
#        include "midi_transport.h"
#        include "nrf5_midi_typing.h"
#        include "process_music.h"
#    endif
static char report_buffer[256];
static uint8_t report_position;
static bool report_pending;
static uint16_t last_report_character;
#endif

void nrf5_lighting_report(void) {
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
#    if defined(RGB_MATRIX_IS31FL3741) || defined(OLED_ENABLE)
    errors = nrf5_i2c_error_count();
#    endif
    snprintf(report_buffer, sizeof(report_buffer),
             "qmknrf5 t=%lu m=%u:%u f=%u u=%u:%u p=%u twi=%lu ee=%lx/%lu irq=%lu reset=%lx\n",
             (unsigned long)timer_read32(), matrix, matrix_mode, flags, under, under_mode, power, errors,
             (unsigned long)nrf5_eeprom_last_error(), (unsigned long)nrf5_eeprom_error_count(),
             (unsigned long)__get_IPSR(), (unsigned long)NRF_POWER->RESETREAS);
#    ifdef MIDI_ENABLE
    // Snapshot before typing the report, which itself sends ordinary HID keys.
    size_t used = strlen(report_buffer);
    char midi[112];
    nrf_midi_diagnostics(midi, sizeof(midi));
    unsigned pure = 0;
#        if defined(AUDIO_ENABLE) || defined(MIDI_BASIC)
    pure = is_midi_on();
#        endif
    snprintf(report_buffer + used, sizeof(report_buffer) - used,
             "midi typing=%u pure=%u %s\n", nrf5_midi_typing_enabled(), pure, midi);
#    endif
#    ifdef OLED_ENABLE
    size_t oled_used = strlen(report_buffer);
    snprintf(report_buffer + oled_used, sizeof(report_buffer) - oled_used, "oled=%u\n", is_oled_on());
#    endif
    report_position = 0;
    report_pending = true;
#endif
}

void nrf5_lighting_task(void) {
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

bool nrf5_process_lighting(uint16_t keycode, keyrecord_t *record, uint16_t combined_toggle) {
#ifdef NRF5_LIGHTING_DIAGNOSTICS
    if (keycode == combined_toggle || keycode == QK_RGB_MATRIX_TOGGLE || keycode == QK_RGB_MATRIX_ON || keycode == QK_RGB_MATRIX_OFF || keycode == QK_UNDERGLOW_TOGGLE) {
        if (record->event.pressed) ++lighting_presses;
        else ++lighting_releases;
        lighting_last_key = keycode;
        lighting_last_edge = record->event.pressed;
    }
#endif
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
