// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#define MATRIX_ROWS 5
#define MATRIX_COLS 5
#define MATRIX_ROW_PINS {41, 5, 43, 38, 36}
#define MATRIX_COL_PINS {29, 45, 9, 10, 13}
#define DIODE_DIRECTION ROW2COL
#define IGNORE_ATOMIC_BLOCK
#define DEBOUNCE 20
#define BOOTMAGIC_ROW 0
#define BOOTMAGIC_COLUMN 4
#define TAPPING_TERM 150
#define HOLD_ON_OTHER_KEY_PRESS
#define QUICK_TAP_TERM 0
#define ONESHOT_TIMEOUT 120

#define BLE_HID_MIN_INTERVAL 7.5
#define BLE_HID_MAX_INTERVAL 30
#define BLE_HID_SLAVE_LATENCY 6
#define BLE_HID_TIMEOUT 430
#define KEYBOARD_SCAN_INTERVAL 6
#define POWER_SAVE_TIMEOUT 2000
#define NKRO_EPSIZE 22

// Preserve the E73 module's internal LF RC clock configuration.
#define NRF_SDH_CLOCK_LF_SRC 0
#define NRF_SDH_CLOCK_LF_RC_CTIV 16
#define NRF_SDH_CLOCK_LF_RC_TEMP_CTIV 2
#define NRF_SDH_CLOCK_LF_ACCURACY 1

#define WS2812_DI_PIN 3
#define WS2812_T0H 375
#define WS2812_T1H 750
#define RGBLIGHT_EFFECT_BREATHING
#define RGBLIGHT_EFFECT_RAINBOW_MOOD
#define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#define RGBLIGHT_EFFECT_SNAKE
#define RGBLIGHT_EFFECT_KNIGHT
#define RGBLIGHT_EFFECT_CHRISTMAS
#define RGBLIGHT_EFFECT_STATIC_GRADIENT
#define RGBLIGHT_EFFECT_RGB_TEST
#define RGBLIGHT_EFFECT_ALTERNATING
#define RGBLIGHT_EFFECT_TWINKLE
#define RGBLIGHT_LAYERS

#define I2C1_SCL 17
#define I2C1_SDA 15
#define OLED_DISPLAY_128X32
#define OLED_DISPLAY_ADDRESS 0x3C
#define OLED_UPDATE_INTERVAL 100
#define OLED_TIMEOUT 60000
#define OLED_I2C_TIMEOUT 100

#define ENCODER_A_PINS {24}
#define ENCODER_B_PINS {22}
#define ENCODER_DIRECTION_FLIP
#define ENCODER_RESOLUTION 2
#define TAP_CODE_DELAY 20

#define KEYBOARD_CONTROL_CAPABILITIES
#define MIDI_BASIC
#define MIDI_ADVANCED
#ifdef VIA_ENABLE
#    define VIA_EEPROM_CUSTOM_CONFIG_SIZE 1
#    define DYNAMIC_KEYMAP_LAYER_COUNT 5
// One nearly-full EEPROM record needs room for old/new copies, peer
// records and FDS garbage collection. Three default FDS pages are too few.
#    define FDS_VIRTUAL_PAGES 5
// Bound individual SoftDevice flash operations while USB is active.
#    define NRF_FSTORAGE_SD_MAX_WRITE_SIZE 128
#    define EEPROM_SIZE 3840
#    define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 2047
#    define NRF5_UI_EEPROM_ADDR 2048
#endif

// Shared UI configuration; indices are this keyboard's layer numbers.
// This USB-only test pad has no battery; report unknown, not a matrix-pin ADC value.
#define NRF5_OLED_BATTERY_AVAILABLE 0
#define NRF5_OLED_TITLE "ciank_pad"
#define NRF5_OLED_TITLE_BITMAP_HEADER "oled_title.h"
#define NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER "oled_logo.h"
#define NRF5_OLED_LAYER_NAMES {"BASE", "NAV", "RGB", "SYS", "BLE"}
#define NRF5_ENCODER_BINDINGS { \
    {0, 0, NRF5_ENCODER_KEYS, KC_VOLD, KC_VOLU}, \
    {0, 1, NRF5_ENCODER_KEYS, KC_PGUP, KC_PGDN}, \
    {0, 2, NRF5_ENCODER_UNDERGLOW, KC_NO, KC_NO}, \
}
