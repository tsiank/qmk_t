// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_ui.h"
#include "ble_service.h"
#ifdef OLED_ENABLE
#    include "oled_driver.h"
#endif
#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#endif
#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix.h"
#endif

#ifndef NRF5_ENCODER_BINDINGS
#    define NRF5_ENCODER_BINDINGS {{0, 0, NRF5_ENCODER_KEYS, KC_VOLU, KC_VOLD}}
#endif
static const nrf5_encoder_binding_t bindings[] = NRF5_ENCODER_BINDINGS;

bool nrf5_encoder_update(uint8_t index, bool clockwise) {
    if (!encoder_update_user(index, clockwise)) return false;
    bool known = false;
    uint8_t layer = get_highest_layer(layer_state | default_layer_state);
    for (uint8_t i = 0; i < ARRAY_SIZE(bindings); i++) {
        const nrf5_encoder_binding_t *binding = &bindings[i];
        if (binding->index != index) continue;
        if (!known) {
            reset_power_save_counter();
#ifdef OLED_ENABLE
            oled_on();
#endif
        }
        known = true;
        if (binding->layer != layer) continue;
        if (binding->action == NRF5_ENCODER_KEYS) {
            uint16_t keycode = clockwise ? binding->clockwise : binding->counterclockwise;
            if (keycode != KC_NO) tap_code16(keycode);
        }
#ifdef RGBLIGHT_ENABLE
        else if (binding->action == NRF5_ENCODER_UNDERGLOW) {
            if (get_mods() & MOD_BIT(KC_LALT)) {
                clockwise ? rgblight_decrease_val() : rgblight_increase_val();
            } else if (get_mods() & MOD_BIT(KC_LCTL)) {
                clockwise ? rgblight_decrease_sat() : rgblight_increase_sat();
            } else {
                clockwise ? rgblight_decrease_hue() : rgblight_increase_hue();
            }
        }
#endif
#ifdef RGB_MATRIX_ENABLE
        else if (binding->action == NRF5_ENCODER_MATRIX) {
            if (get_mods() & MOD_BIT(KC_LALT)) {
                clockwise ? rgb_matrix_decrease_val() : rgb_matrix_increase_val();
            } else if (get_mods() & MOD_BIT(KC_LCTL)) {
                clockwise ? rgb_matrix_decrease_sat() : rgb_matrix_increase_sat();
            } else {
                clockwise ? rgb_matrix_decrease_hue() : rgb_matrix_increase_hue();
            }
        }
#endif
        return false;
    }
    return !known;
}
