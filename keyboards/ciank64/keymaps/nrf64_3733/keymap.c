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

#include "nrf_delay.h"
#include "nrf_power.h"
#include "nrfx_power.h"
#include "nrf.h"

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
extern rgblight_config_t rgblight_config;
#endif


enum ciank64_layers {
    _QWERTY,
    _SIGN,
    _FN,
    _RGBST,
    _MOUSE
};


enum planck_keycodes { DISC = SAFE_RANGE, ADVW, ADVS, SEL0, SEL1, SEL2, DELB, SLEEP, REBOOT, ENT_DFU, RGBRST,RGBG_TOG, BATT_LV, BATT_LV2,VOLUP};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [_QWERTY] = LAYOUT(
        GRAVE_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
        KC_LSFT,          RGBM_TOG,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,  KC_UP, KC_DELETE,
        KC_LCTL,    KC_LALT, MO(_SIGN),             KC_SPC, MO(_FN),          KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_SIGN]   = LAYOUT(
        _______,  KC_F1,  KC_F2,  KC_F3,  KC_F4,  KC_F5,    KC_F6,    KC_F7,   KC_F8,   KC_F9  , KC_F10, KC_F11,    KC_F12,  _______, 
        SLEEP,  ADVW,  DELB,  DISC,    OUT_USB,  OUT_BT, _______,   _______,    _______,  _______,    KC_PSCREEN,    KC_SCROLLLOCK,   KC_PAUSE,   _______,
        _______,    SEL0,  SEL1,  SEL2,    _______ ,   _______,   _______,     _______,   _______,    KC_HOME,    KC_PGUP,   KC_INSERT,  _______,
        RESET,       _______,  _______,   _______,   _______,         _______,    _______,    _______,   _______,    KC_END,   KC_PGDOWN, KC_DELETE,_______, _______,
        _______,    _______, KC_TRNS,             KC_SPC,       KC_TRNS,  _______, _______, _______,   _______
                      ),
    [_FN]   = LAYOUT(
        _______,  _______,  _______, _______,    _______, _______,  _______, _______, _______, _______, _______, _______,ENT_DFU, RESET,
        RGBG_TOG,  _______, _______,  _______,   _______,_______,   _______, _______, _______, _______, _______, _______,_______, REBOOT,
        RGBM_TOG,  RGBM_MOD,RGBM_RMOD,  RGBM_M_P,  RGBM_M_B, RGBM_M_R, RGBM_M_SW, _______, _______, _______, _______, _______,_______,
        RGB_M_P, RGB_M_B, RGB_M_R, RGB_M_SW, RGB_M_SN, RGB_M_K, RGB_M_X, RGB_M_G, RGB_M_T, _______,_______, _______, _______,_______,
        RGB_TOG, RGBRST, RGB_MOD,  RGB_RMOD, KC_SPC, KC_TRNS, BATT_LV, TO(_RGBST), TO(_MOUSE)
                      ),
    [_RGBST] = LAYOUT(
        _______, RGBM_HUI, RGBM_HUD, RGBM_SAI, RGBM_SAD, RGBM_VAI, RGBM_VAD, RGBM_SPI, RGBM_SPD, _______, _______,_______, _______,_______, 
        _______, _______,_______,_______, _______,_______,  _______, _______,_______,_______, _______,_______,_______, _______,
        _______, RGB_HUI, RGB_HUD, RGB_SAI, RGB_SAD, RGB_VAI, RGB_VAD, RGB_SPI, RGB_SPD, _______, _______,_______, _______,
        _______,  RGB_M_P, RGB_M_B, RGB_M_R, RGB_M_SW, RGB_M_SN, RGB_M_K, RGB_M_X, RGB_M_G, RGB_M_T, _______,_______,_______,_______,
        _______, _______, _______, _______, _______, _______, _______, TO(_QWERTY),  _______
                     ),
    [_MOUSE] = LAYOUT(
        _______, KC_MS_UP, KC_MS_DOWN, KC_MS_LEFT, KC_MS_RIGHT,_______, _______,_______,  _______, _______,_______, _______,_______, _______,
        _______, KC_MS_BTN1, KC_MS_BTN2, _______, _______,  _______, _______, _______, _______, _______,_______, _______, _______,_______, 
        _______, KC_MS_WH_UP, KC_MS_WH_DOWN, _______, _______, _______, _______,_______,_______, _______,_______, _______,_______,
        KC_PWR, KC_MUTE, KC_VOLU, KC_VOLD, KC_MNXT, KC_MPRV, KC_MSTP, KC_MPLY, _______, _______, _______, _______,_______, _______,
        KC_MY_COMPUTER,  _______,  _______, _______, _______, _______, _______, _______, TO(_QWERTY)
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
            
    case RGBRST:
      #ifdef RGBLIGHT_ENABLE
        if (record->event.pressed) {
          eeconfig_update_rgblight_default();
          rgblight_enable();
        }
      #endif
      break;
      
    	#ifdef IS31FL3733
    case RGBM_TOG:
        if (record->event.pressed) {
            if (rgb_matrix_config.enable) {
                i2c_stop();
            } else {
                i2c_start();
            }
        }
        return true;
	  	#endif
    case RGBG_TOG:
        if (record->event.pressed) {
	    	#ifdef IS31FL3733
            if (rgb_matrix_config.enable) {
                i2c_stop();
            } else {
                i2c_start();
            }
            	 rgb_matrix_toggle();
	  	#endif
	  	
	      #ifdef RGBLIGHT_ENABLE
	        	rgblight_toggle();
		#endif
        }
    	return true;

        case SLEEP:
              #ifdef RGBLIGHT_ENABLE
                rgblight_disable();
      		#endif
            if (!record->event.pressed) {
                deep_sleep_mode_enter();
            }
            return false;
          
          case VOLUP:
            if (record->event.pressed) {
      		register_code(KC_A);
      		unregister_code(KC_A);
      		nrf_delay_ms(2000);
      		register_code(KC_B);
      		unregister_code(KC_B);
    		}
    	 break;

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
    	#ifdef IS31FL3733
void rgb_matrix_indicators_user(void) {
	  uint8_t this_led = host_keyboard_leds();
	  if (!g_suspend_state) {
	    switch (biton32(layer_state)) {
	      case _SIGN:
	        rgb_matrix_set_color(76,0xFF, 0x56, 0x20);
          break;
	      case _FN:
	        rgb_matrix_set_color(77,0xFF, 0x80, 0x00);
          break;
	      case _RGBST:
	        rgb_matrix_set_color(78,0xFF, 0x77, 0xBF);
          break;
	      case _MOUSE:
	        rgb_matrix_set_color(79,0xFF, 0x60, 0x40);
          break;
      }
    }
    if ( this_led & (1<<USB_LED_CAPS_LOCK)) {
          rgb_matrix_set_color(28, 0xFF, 0xFF, 0xFF);
          rgb_matrix_set_color(76, 0xFF, 0xFF, 0xFF);
    }
}
#endif

const rgblight_segment_t PROGMEM my_capslock_layer[] = RGBLIGHT_LAYER_SEGMENTS(
    { 0, 8, HSV_WHITE} //从第一个灯开始，点亮8个灯，颜色为白色
);

const rgblight_segment_t* const PROGMEM my_rgb_layers[] = RGBLIGHT_LAYERS_LIST(
    my_capslock_layer
    //Overrides other layers
);

void keyboard_post_init_user() {
    #ifdef IS31FL3733
    // Shutdown IS31FL3737 if rgb disabled
    if (!rgb_matrix_config.enable) {
        i2c_stop();
    }
    #endif

    #ifdef RGBLIGHT_ENABLE
        //Enable the LED layers
        rgblight_layers = my_rgb_layers;
    #endif
}

bool led_update_user(led_t led_state){
    rgblight_set_layer_state(0, led_state.caps_lock);
    return true;
}