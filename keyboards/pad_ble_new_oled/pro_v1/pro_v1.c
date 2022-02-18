/*
Copyright 2018 Sekigon

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_power.h"
#include "nrfx_power.h"

#include "matrix.h"
#include "rgblight.h"
#include "nrf.h"
#include "wait.h"
#ifdef ENCODER_ENABLE
  #include "encoder.h"
#endif
#ifdef DIP_SWITCH_ENABLE
  #include "dip_switch.h"
#endif
#ifdef WPM_ENABLE
  #include "wpm.h"
#endif

#ifdef RGBLIGHT_ENABLE
extern rgblight_config_t rgblight_config;
#endif
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "i2c_master.h"

// #include "usbd.c"
#undef PACKED

// #define IS_LED_ON(leds, led_name)   ( (leds) & (1 << (led_name)))
// #define IS_LED_OFF(leds, led_name)  (~(leds) & (1 << (led_name)))

//#define SET_CAPS_LOCK_LED() writePinLow(LED_PIN)
//#define RESET_CAPS_LOCK_LED() writePinHigh(LED_PIN)

// void unselect_cols(void);
// void select_col(uint8_t row);
// matrix_col_t read_rows(void);
// static bool bootloader_flag = false;

//caps leds not working
/*
void led_init_kb(void)
{
    // setPinOutput(LED_PIN);
    // RESET_CAPS_LOCK_LED();
    nrf_gpio_cfg_output(LED_PIN);
    nrf_gpio_pin_set(LED_PIN);
}

void led_set_kb(uint8_t usb_led)
{
    // NRF_LOG_INFO("leds: %d", usb_led);
    if (IS_LED_ON(usb_led, USB_LED_CAPS_LOCK)) {
        nrf_gpio_pin_clear(LED_PIN);
    } else {
        nrf_gpio_pin_clear(LED_PIN);
    }
    led_set_user(usb_led);
}
*/

void matrix_init_user() {
#ifdef ENCODER_ENABLE
  encoder_init();
#endif

#ifdef OLED_DRIVER_ENABLE
    oled_init(OLED_ROTATION_0);
#endif

#ifdef DIP_SWITCH_ENABLE
  dip_switch_init();
  dip_switch_read(false);
#endif
}

void matrix_scan_user(void) {
  
  #ifdef ENCODER_ENABLE
    encoder_read();
  #endif
  
  #ifdef WPM_ENABLE
    decay_wpm();
  #endif
  #ifdef DIP_SWITCH_ENABLE
  // dip_switch_read();
  #endif

  #ifdef OLED_DRIVER_ENABLE
    oled_task();
  #endif
}




