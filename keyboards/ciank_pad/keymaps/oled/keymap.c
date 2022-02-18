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
#include "app_timer.h"

#include "eeprom.h"
#include "string.h"
#include "eeconfig.h"
#include "action.h"
#include "timer.h"
#include "host.h"

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

#ifdef WPM_ENABLE
  #include "wpm.h"
#endif

const char *read_layer_state(void);
const char *read_logo(void);
void set_keylog(uint16_t keycode, keyrecord_t *record);
const char *read_keylog(void);
const char *read_keylogs(void);

enum ciank_pad_layers {
    _BASE = 0,
    _DIR,
    _RGB_SETTINGS,
    _SYS,
    _BLE_SETTINGS
};


enum planck_keycodes { DISC = SAFE_RANGE, ADVW, ADVS, SEL0, SEL1, SEL2, DELB, SLEEP, REBOOT, ENT_DFU, RGBRST,RGBG_TOG, BATT_LV, BATT_LV2,VOLUP};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [_BASE]=LAYOUT(
    KC_MUTE,
    KC_VOLU, KC_VOLD, VOLUP, ADVW,KC_9,
    KC_4, KC_5, TO(_DIR), TO(_DIR), TO(2),
    KC_1, KC_2, KC_3, KC_PAST, TO(3),
    KC_0, M(0), KC_DOT, KC_PSLS, KC_EQL),

    [_DIR]=LAYOUT(
    KC_TRNS,
    TO(_BASE), KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_1, KC_0, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_UP, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_LEFT, KC_DOWN, KC_RGHT, KC_TRNS, KC_TRNS),

    [_RGB_SETTINGS]=LAYOUT(
    KC_TRNS,
    TO(_BASE), KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    RGB_TOG, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),

    [_SYS]=LAYOUT(
    KC_TRNS,
    TO(_BASE), TO(_BLE_SETTINGS), KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_PGUP, KC_HOME, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_PGDN, KC_END, KC_TRNS, KC_TRNS),

    [_BLE_SETTINGS]=LAYOUT(
    TO(_BASE),
    SEL0, SEL1, ADVW, OUT_BT, OUT_USB,
    KC_TRNS, KC_TRNS, DELB, OUT_USB, DELB,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, ADVW,
    KC_TRNS, KC_TRNS, KC_TRNS, BATT_LV, BATT_LV2),
};


char hid_state_str[24];
const char *read_hid_state(void) {
#if defined IS_LEFT_HAND  &&  IS_LEFT_HAND == true
  snprintf(hid_state_str, sizeof(hid_state_str), "(): %s %s",
           (get_usb_enabled()) ? "!\"" : " ",
           (get_ble_enabled()) ? "#$" : " ");
#endif
  return hid_state_str;
}

#ifdef WPM_ENABLE
char wpm_str[24];
void set_wpm(void) {
    snprintf(wpm_str, sizeof(wpm_str), "*+: %d", get_current_wpm());
}
const char *read_wpm(void) {
  return wpm_str;
}
#endif

#ifdef RGBLIGHT_ENABLE
char rgb_state_str[24];
const char *read_rgb_state(void)
{
  snprintf(rgb_state_str, sizeof(rgb_state_str), "LIGHT %s MODE: %d",
    rgblight_config.enable ? "ON " : "OFF", rgblight_config.mode);
  return rgb_state_str;
}
#endif

char mod_shift_win_str[24];
const char *read_shift_win_state(void)
{
  snprintf(mod_shift_win_str, sizeof(mod_shift_win_str), "%s %s",
    get_mods() & MOD_BIT(KC_LSHIFT)? "]^" : "  ",
    get_mods() & MOD_BIT(KC_LGUI)? "\\" : " " );
  return mod_shift_win_str;
}

char mod_ctrl_alt_str[24];
const char *read_ctrl_alt_state(void)
{
  snprintf(mod_ctrl_alt_str, sizeof(mod_ctrl_alt_str), "%s %s",
    get_mods() & MOD_BIT(KC_LCTL)? "_" : " ",
    get_mods() & MOD_BIT(KC_LALT)? "{" : " " );
  return mod_ctrl_alt_str;
}

char version_str[24];
const char *read_version(void)
{
  snprintf(version_str, sizeof(version_str), "VER 0510 EEPROM");
  return version_str;
}

//caps lock state not working

char caps_lock_state[24];
const char *read_caps_lock_state(void)
{
  uint8_t leds = host_keyboard_leds();
  snprintf(caps_lock_state, sizeof(caps_lock_state), "CL:%s",
    (leds & (1 << USB_LED_CAPS_LOCK)) ? "1" : "0");
  return caps_lock_state;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
	if (!NRF_USBD->ENABLE){
	    #ifdef POWER_SAVE_TIMEOUT
	        if (record->event.pressed) {
	            reset_power_save_counter();
	        }
	    #endif
	}
	
    #ifdef OLED_DRIVER_ENABLE
    oled_on();
  #endif
  
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
            		register_code(KC_VOLU);
    		}else{
    		       unregister_code(KC_VOLU);
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


keyevent_t encoder_volu  = {
    .key = (keypos_t){.row = 1, .col = 1},
    .pressed = false,
    .time = 1
};

keyevent_t encoder_vold  = {
    .key = (keypos_t){.row = 1, .col = 2},
    .pressed = false,
    .time = 1
};

bool encoder_rot = true;

static void my_timeout_handler_volu (void * p_context){
				// unregister_code(KC_VOLU);
    encoder_volu.pressed = false;
    encoder_rot = true;
    encoder_volu.time = (timer_read() | 1); 
    action_exec(encoder_volu);
}

static void my_timeout_handler_vold (void * p_context){
				// unregister_code(KC_VOLU);
    encoder_vold.pressed = false;
    encoder_rot = true;
    encoder_vold.time = (timer_read() | 1); 
    action_exec(encoder_vold);
}

//Media keys WORKS
#ifdef ENCODER_ENABLE
void encoder_update_user(uint8_t index, bool clockwise) {
  switch(biton32(layer_state))
  {
    case _BASE:
      if(clockwise){
        encoder_volu.pressed = true;
        encoder_rot = true;
        encoder_volu.time = (timer_read() | 1);  //time should not be 0 
        action_exec(encoder_volu);
        APP_TIMER_DEF(my_timer_id);
        app_timer_create(&my_timer_id, APP_TIMER_MODE_SINGLE_SHOT, my_timeout_handler_volu);
        app_timer_start(my_timer_id, APP_TIMER_TICKS(16), NULL); 
      }
      else{
        encoder_vold.pressed = true;
        encoder_rot = true;
        encoder_vold.time = (timer_read() | 1);  //time should not be 0 
        action_exec(encoder_vold);
        APP_TIMER_DEF(my_timer_id);   
        app_timer_create(&my_timer_id, APP_TIMER_MODE_SINGLE_SHOT, my_timeout_handler_vold);
        app_timer_start(my_timer_id, APP_TIMER_TICKS(16), NULL);
      }
      break;
    case _DIR:
      if(clockwise){
        tap_code(KC_PGUP);
      }
      else{
        tap_code(KC_PGDN);
      }
      break;
    case _SYS:
    case _BLE_SETTINGS:
    break;
    case _RGB_SETTINGS:
      if (get_mods() & MOD_BIT(KC_LALT)) {
        if(clockwise){
          rgblight_decrease_val();
        } else {
          rgblight_increase_val();
        }
      } else if (get_mods() & MOD_BIT(KC_LCTL)){
        if(clockwise){
          rgblight_decrease_sat();
        } else {
          rgblight_increase_sat();
        }
      } else {
        if(clockwise){
          rgblight_decrease_hue();
        } else {
          rgblight_increase_hue();
        }
      }
      break;
    default:
    break;
  }
}
#endif

#ifdef OLED_DRIVER_ENABLE
oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    //   oled_scroll_set_area(0,2);
    // oled_scroll_set_speed(4);
    // oled_scroll_right();
    return OLED_ROTATION_0;
}
void oled_task_user() {
    switch(biton32(layer_state))
    {
      case _BASE:
      case _DIR:
        oled_write_ln(read_layer_state(), false);
        oled_write_ln(read_hid_state(), false);
        #ifdef WPM_ENABLE
          oled_write_ln(read_wpm(), false);
        #else
          oled_write_ln("JasonRen_Pad", false);
        #endif
        // oled_write_ln(ead_shift_win_state(, false);
        // matrix_write(matrix,read_ctrl_alt_state(, false);
        //others:
        // matrix_write(matrix,read_caps_lock_state(, false);
        // oled_write_ln(read_keylog(, false);
        // oled_write_ln(read_keylogs(, false);
        //oled_write_ln(read_mode_icon(keymap_config.swap_lalt_lgui, false);
        //oled_write_ln(read_host_led_state(, false);
        //oled_write_ln(read_timelog(, false);
      break;
      case _SYS:
        oled_write_ln(",- =>          [ESC", false);
        oled_write_ln("B: ENTER BOOT", false);
        oled_write_ln(read_version(), false);
      break;
      case _BLE_SETTINGS:
        oled_write_ln(",- #$          [ESC", false);
        oled_write_ln("T: TOGGLE HID", false);
        oled_write_ln("DEL: DEL BONDS", false);
        oled_write_ln("JasonRen_Pad", false);
      break;
      case _RGB_SETTINGS:
      #ifdef RGBLIGHT_ENABLE
        oled_write_ln(",- ;<          [ESC", false);
        oled_write_ln(read_rgb_state(), false);
        oled_write_ln("T: TOGGLE E: RESET ", false);
        oled_write_ln( "M: MODE HUE:ENCODER", false);
      #else
        oled_write_ln(",- ;<          [ESC", false);
        oled_write_ln("RGB NOT SUPPORTED", false);
        oled_write_ln("JasonRen_Pad", false);
        oled_write_ln("", false);

      #endif
      break;
      default:
      oled_write_ln("ERROR LAYER", false);
      oled_write_ln("JasonRen_Pad", false);
      oled_write_ln("JasonRen_Pad", false);
      oled_write_ln("JasonRen_Pad", false);
      break;
    }
}

#endif