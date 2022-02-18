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
extern rgblight_config_t rgblight_config;
#endif


enum ciank67_layers {
    _0,
    _1,
    _2,
    _3,
    _4,
    _5,
    _6,
    _7,
    _8,
    _9,
};


enum planck_keycodes { DISC = SAFE_RANGE, ADVW, ADVS, SEL0, SEL1, SEL2, DELB, SLEEP, REBOOT, ENT_DFU, RGBRST,RGBG_TOG, BATT_LV, BATT_LV2,VOLUP};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [_0] = LAYOUT(
        KC_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,  KC_UP, KC_MUTE,
        KC_LCTL,   KC_LWIN, KC_LALT,   KC_SPC,              KC_SPC, KC_SPC,  MO(_8), MO(_7),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_1] = LAYOUT(
        GRAVE_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LPRN, KC_LABK, KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_COLN, KC_DQUO,          KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_QUES, KC_EXLM,  KC_UP, KC_1,
        KC_LCTL,   KC_LWIN, KC_LALT,   KC_LALT,              KC_SPC, KC_SPC,  MO(_8), MO(_7),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_2] = LAYOUT(
        GRAVE_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LPRN, KC_LABK, KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_COMM,  KC_DOT,          KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,   KC_COLN, KC_DQUO,  KC_QUES, KC_EXLM,  KC_UP, KC_2,
        KC_LCTL,   KC_LWIN, KC_LALT,   KC_LALT,              KC_SPC, KC_SPC,  MO(_8), MO(_7),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_3] = LAYOUT(
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,  KC_UP, KC_3,
        KC_LCTL,   KC_NO, KC_LALT,   KC_LALT,              KC_SPC, KC_M,  KC_ESC, MO(_8),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_4] = LAYOUT(
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LPRN, KC_LABK, KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_COLN, KC_DQUO,          KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_QUES, KC_EXLM,  KC_UP, KC_4,
        KC_LCTL,   KC_NO, KC_LALT,   KC_LALT,              KC_SPC, KC_M,  KC_ESC, MO(_8),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_5] = LAYOUT(
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,  KC_UP, KC_5,
        KC_LCTL,   KC_ESC, KC_M,   KC_LALT,              KC_SPC, KC_B,  KC_F1, MO(_8),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_6] = LAYOUT(
        KC_ESC, RGBM_HUI, RGBM_HUD, RGBM_SAI, RGBM_SAD, RGBM_VAI, RGBM_VAD, RGBM_SPI, RGBM_SPD, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, 
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,  KC_NO, KC_NO, KC_NO, KC_NO,  KC_NO, KC_NO, KC_NO,  KC_NO,
        KC_NO, RGB_HUI, RGB_HUD, RGB_SAI, RGB_SAD, RGB_VAI, RGB_VAD, RGB_SPI, RGB_SPD,  KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO,  RGB_M_P, RGB_M_B, RGB_M_R, RGB_M_SW, RGB_M_SN, RGB_M_K, RGB_M_X, RGB_M_G, RGB_M_T, KC_NO, KC_NO, KC_NO,KC_6,
        TO(_0), TO(_0), TO(_0), TO(_0), TO(_0), TO(_0), TO(_0), TO(_0), TO(_0), TO(_0),TO(_0),   TO(_0)
                     ),
    [_7] = LAYOUT(
        KC_ESC, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_ACL0, KC_ACL1, KC_ACL2,  KC_NO, KC_NO, KC_NO, KC_NO, 
        KC_TAB, KC_BTN4, KC_MS_U, KC_BTN5,  KC_NO, KC_NO,  KC_NO,  KC_WH_L , KC_WH_U,  KC_WH_R,  KC_NO, KC_NO, KC_NO,  KC_NO,
        KC_CAPS, KC_MS_L, KC_MS_D, KC_MS_R, KC_NO, KC_NO, KC_NO, KC_BTN1, KC_WH_D, KC_BTN2, KC_NO,KC_NO, KC_NO,
        KC_LSFT,  KC_BTN1, KC_BTN2, KC_BTN3, KC_NO, KC_NO, KC_NO, KC_BTN1, KC_BTN2, KC_BTN3,  KC_NO, KC_NO, KC_NO,KC_7,
        KC_LCTL,   KC_LWIN, KC_LALT,   KC_SPC,              KC_SPC, KC_SPC,  MO(_7), MO(_8),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                     ),
    [_8] = LAYOUT(
        KC_ESC,  KC_F1,  KC_F2,  KC_F3,  KC_F4,  KC_F5,    KC_F6,    KC_F7,   KC_F8,   KC_F9  , KC_F10, KC_F11,    KC_F12,  KC_BSPC,
        KC_TAB,  KC_PSCR,    KC_SLCK,    KC_PAUS,    KC_NO,    KC_NLCK,    KC_P7,    KC_P8,    KC_P9,    KC_PMNS,    KC_PSLS,    KC_NO, KC_NO, TO(_9),
        KC_CAPS, KC_INS,    KC_HOME,    KC_PGUP,    KC_NO,    KC_NO,    KC_P4,    KC_P5,    KC_P6,    KC_PPLS,    KC_PAST, KC_CALC,          KC_ENT,
        KC_LSFT,          KC_DEL,    KC_END,    KC_PGDN,    KC_NO,    KC_P0,    KC_P1,    KC_P2,    KC_P3, KC_PDOT,  KC_NO, KC_NO,  KC_UP, KC_8,
        KC_LCTL,   KC_LWIN, KC_LALT,   KC_SPC,              KC_SPC, KC_SPC,  MO(_8), MO(_7),                   MO(_9), KC_LEFT, KC_DOWN, KC_RGHT
                       ),
    [_9]   = LAYOUT(
         TO(_0),   TO(_1),   TO(_2),   TO(_3),   TO(_4),   TO(_5),     TO(_6),     TO(_7),    TO(_8),    TO(_9),  TO(_0),  OUT_USB,     OUT_BT,  KC_SLEP, 
        RGBG_TOG,  KC_MYCM,  KC_WHOM,  KC_MAIL,    KC_NO,  KC_NO,  KC_NO,   KC_NO,    KC_NO,  SLEEP,    DISC,    DELB,   ADVW,   KC_PWR,
        RGBM_TOG,    KC_NO,  KC_NO,  KC_NO,    KC_NO,   KC_NO,   KC_NO,     KC_NO,   KC_NO,    KC_NO,     BATT_LV,   KC_CALC,  KC_MSEL,
        RGB_TOG,       SEL0,  SEL1,   SEL2,   KC_NO,         RESET,   KC_NO,    KC_NO,   KC_MYCM,    KC_WHOM,    KC_MSTP, TO(_0),  KC_VOLU, KC_MUTE,
        RESET,    ENT_DFU, SLEEP, KC_MSTP,              KC_MPLY,       KC_MRWD,  KC_MFFD,  KC_MSEL,  MO(_9),    KC_MPRV,  KC_VOLD, KC_MNXT 
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
	    	#ifdef IS31FL3737
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

void rgb_matrix_indicators_user(void) {
	  uint8_t this_led = host_keyboard_leds();
	  if (!g_suspend_state) {
	    switch (biton32(layer_state)) {
        case _1:
          rgb_matrix_set_color(0, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(1, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(1, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(2, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(3, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(4, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(5, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(6, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(7, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(8, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(9, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(10, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(11, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(12, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(14, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(15, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(16, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(17, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(18, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(19, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(20, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(21, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(22, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(23, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(24, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(25, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(26, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(27, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(28, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(29, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(30, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(31, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(32, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(33, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(34, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(35, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(36, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(37, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(38, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(39, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(40, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(41, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(42, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(43, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(44, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(45, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(46, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(47, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(48, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(49, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(50, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(51, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(52, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(53, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(54, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(55, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(56, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(57, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(58, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(59, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(60, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(61, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(62, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(63, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(64, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(65, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(66, 0x00, 0x00, 0x00);
          rgb_matrix_set_color(67, 0x00, 0xFF, 0xFF);
          rgb_matrix_set_color(68, 0xFF, 0x00, 0x00);
          rgb_matrix_set_color(69, 0xFF, 0x00, 0x00);
          rgb_matrix_set_color(70, 0x00, 0xFF, 0x00);
          rgb_matrix_set_color(71, 0x00, 0xFF, 0x00);
          rgb_matrix_set_color(72, 0xFF, 0xFF, 0x00);
          rgb_matrix_set_color(73, 0xFF, 0xFF, 0x00);
          rgb_matrix_set_color(74, 0xFF, 0xFF, 0x00);
          rgb_matrix_set_color(75, 0x00, 0xFF, 0x00);
          rgb_matrix_set_color(76, 0x00, 0x00, 0xFF);
          rgb_matrix_set_color(77, 0x00, 0xFF, 0x00);
          rgb_matrix_set_color(78, 0xFF, 0x00, 0x00);
          rgb_matrix_set_color(79, 0xFF, 0xFF, 0x00);
          rgb_matrix_set_color(80, 0xFF, 0xFF, 0x00);
          rgb_matrix_set_color(81, 0xFF, 0x00, 0x00);
          rgb_matrix_set_color(82, 0xFF, 0x00, 0x00);
          break;
	      case _3:
	        rgb_matrix_set_color(76,0xFF, 0x56, 0x20);
          break;
	      case _9:
	        rgb_matrix_set_color(77,0xFF, 0x80, 0x00);
          break;
	      case _6:
	        rgb_matrix_set_color(78,0xFF, 0x77, 0xBF);
          break;
	      case _2:
	        rgb_matrix_set_color(79,0xFF, 0x60, 0x40);
          break;
      }
    }
    if ( this_led & (1<<USB_LED_CAPS_LOCK)) {
          rgb_matrix_set_color(28, 0xFF, 0xFF, 0xFF);
          rgb_matrix_set_color(76, 0xFF, 0xFF, 0xFF);
    }
}

const rgblight_segment_t PROGMEM my_capslock_layer[] = RGBLIGHT_LAYER_SEGMENTS(
    { 0, 8, HSV_WHITE} //从第一个灯开始，点亮8个灯，颜色为白色
);

const rgblight_segment_t* const PROGMEM my_rgb_layers[] = RGBLIGHT_LAYERS_LIST(
    my_capslock_layer
    //Overrides other layers
);

void keyboard_post_init_user() {
    #ifdef IS31FL3737
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