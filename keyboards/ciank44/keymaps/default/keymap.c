/* Copyright 2015-2017 Jack Humbert
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H
//#include "muse.h"
#include "ble_service.h"

#include "nrf_gpio.h"
#include "adc.h"

// adafruit bootloader, send "dfu" to debug serial port
#define DFU_MAGIC_UF2_RESET             0x57
void bootloader_jump_uf2(void) {
  sd_power_gpregret_set(0, DFU_MAGIC_UF2_RESET);
  NVIC_SystemReset();
}

#ifdef RGB_MATRIX_ENABLE
#include "rgb_matrix.h"
#include "i2c_master.h"

extern rgb_config_t rgb_matrix_config;
#endif

// extern keymap_config_t keymap_config;

#ifdef RGBLIGHT_ENABLE
#include "rgblight.h"
extern rgblight_config_t rgblight_config;
#endif


enum ciank44_layers {
    _QWERTY,
    _SIGN,
    _FN
};


enum planck_keycodes { DISC = SAFE_RANGE, ADVW, ADVS, SEL0, SEL1, SEL2, DELB, SLEEP, REBOOT, ENT_DFU, RGBRST,RGBG_TOG, BATT_LV, BATT_LV2,VOLUP};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [_QWERTY] = LAYOUT(
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,  KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_UP,
        KC_LCTL,    KC_LWIN, KC_LALT,  MO(_SIGN),         RSFT_T(KC_SPC),     MO(_FN),  KC_RALT, KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_SIGN]   = LAYOUT(
        GRAVE_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS,
        RGBG_TOG,    _______,  _______,  KC_RBRC,   KC_EQL,   KC_AMPERSAND,   KC_ASTERISK,     KC_LEFT_PAREN,   KC_MINS,    KC_PLUS, _______, 
        RESET,       _______,  _______,   KC_UNDERSCORE, KC_LABK,    KC_RIGHT_PAREN,    KC_QUOT,    KC_DOUBLE_QUOTE,   KC_RGHT,    KC_GRAVE,   _______, 
        _______,    _______, _______, KC_TRNS,       RSFT_T(KC_SPC),       KC_TRNS,  _______,   SEL0, SEL1, SEL2
                      ),
    [_FN]   = LAYOUT(
        _______,  _______,  _______, _______,    _______, _______,  _______, KC_PSCREEN, KC_SCROLLLOCK, KC_PAUSE, _______, _______,
        RGBG_TOG,  OUT_USB, OUT_BT,  DISC,   _______, KC_INSERT, KC_HOME, KC_PGUP, _______,_______, REBOOT,
        RGBM_TOG,  RGBM_MOD,RGBM_RMOD,  _______,  _______, KC_DELETE, KC_END,KC_PGDOWN, _______, _______, _______,
        _______, _______, _______, _______, _______, KC_TRNS, _______,_______, DELB,ADVW
                      )
};

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
	if (!NRF_USBD->ENABLE){
	    #ifdef POWER_SAVE_TIMEOUT
	        if (record->event.pressed) {
	            reset_power_save_counter();
	        }
	    #endif
	}
    switch (keycode) {
        case DISC:
            if (record->event.pressed) {
                ble_disconnect();
            }
            return false;

        case ADVW:
            if (record->event.pressed) {
                advertising_without_whitelist();
            }
            return false;

        case ADVS:
            if (record->event.pressed) {
                advertising_start(false);
            }
            return false;

        case SEL0:
            if (record->event.pressed) {
                restart_advertising_id(0);
            }
            return false;

        case SEL1:
            if (record->event.pressed) {
                restart_advertising_id(1);
            }
            return false;

        case SEL2:
            if (record->event.pressed) {
                restart_advertising_id(2);
            }
            return false;

        case DELB:
            if (record->event.pressed) {
                advertising_start(true);
            }
            return false;

        case REBOOT:
            if (record->event.pressed) {
                NVIC_SystemReset();
            }
            
        case ENT_DFU:
            if (record->event.pressed) {
      		bootloader_jump_uf2();
            }

#ifdef IS31FL3737
    case RGBM_TOG:
        if (record->event.pressed) {
            if (rgb_matrix_config.enable) {
                i2c_stop();
            } else {
                i2c_start();
            }
        }
        return true;
        
    case RGBG_TOG:
        if (record->event.pressed) {
            if (rgb_matrix_config.enable) {
                i2c_stop();
            } else {
                i2c_start();
            }
            	 rgb_matrix_toggle();
	  	
	      #ifdef RGBLIGHT_ENABLE
	        	rgblight_toggle();
		#endif
        }
    	return true;
#endif
	  	
        case SLEEP:
              #ifdef RGBLIGHT_ENABLE
                rgblight_disable();
      		#endif
            if (!record->event.pressed) {
                deep_sleep_mode_enter();
            }
            return false;

        case BATT_LV:
            if (record->event.pressed) {
      		char str[16];
      		sprintf(str, "%4dmV", get_vcc());
      		send_string(str);
    		}
    	 break;
        case BATT_LV2:
            if (record->event.pressed) {
      		char str2[8];
      		sprintf(str2, "%d%%", get_battery_level());
      		send_string(str2);
    		}
    	 break;
    }
    return true;
}


void keyboard_post_init_user() {
    #ifdef IS31FL3737
    // Shutdown IS31FL3741 if rgb disabled
    if (!rgb_matrix_config.enable) {
        i2c_stop();
    }
    #endif
}

bool led_update_user(led_t led_state){
    rgblight_set_layer_state(0, led_state.caps_lock);
    return true;
}
