#ifndef CONFIG_H
#define CONFIG_H

#include "config_common.h"

/* Bluetooth connection setting*/
#define BLE_HID_MIN_INTERVAL 7.5    /**UNIT_1_25_MS< Minimum connection interval (7.5 ms) */
#define BLE_HID_MAX_INTERVAL 30   /**UNIT_1_25_MS< Maximum connection interval (30 ms). */
#define BLE_HID_SLAVE_LATENCY 6  /**< Slave latency. */
#define BLE_HID_TIMEOUT 430       /**UNIT_10_MS< Connection supervisory timeout (430 ms). */

/* USB Device descriptor parameter */
#define VENDOR_ID       0xEEEE
#define PRODUCT_ID      0xC376
#define DEVICE_VER      0x0001
#define MANUFACTURER    "Tsiank"
#define PRODUCT         "Tsiank67_rgb"
#define DESCRIPTION     "Standard keyboard"

/* key matrix */
#define MATRIX_ROWS 5
#define MATRIX_COLS 14

// NRF52840 pin map: ((port << 5) | (pin & 0x1F))
// <0=> 0 (P0.0)
// <1=> 1 (P0.1)
// <2=> 2 (P0.2)
// <3=> 3 (P0.3)
// <4=> 4 (P0.4)
// <5=> 5 (P0.5)
// <6=> 6 (P0.6)
// <7=> 7 (P0.7)
// <8=> 8 (P0.8)
// <9=> 9 (P0.9)
// <10=> 10 (P0.10)
// <11=> 11 (P0.11)
// <12=> 12 (P0.12)
// <13=> 13 (P0.13)
// <14=> 14 (P0.14)
// <15=> 15 (P0.15)
// <16=> 16 (P0.16)
// <17=> 17 (P0.17)
// <18=> 18 (P0.18)
// <19=> 19 (P0.19)
// <20=> 20 (P0.20)
// <21=> 21 (P0.21)
// <22=> 22 (P0.22)
// <23=> 23 (P0.23)
// <24=> 24 (P0.24)
// <25=> 25 (P0.25)
// <26=> 26 (P0.26)
// <27=> 27 (P0.27)
// <28=> 28 (P0.28)
// <29=> 29 (P0.29)
// <30=> 30 (P0.30)
// <31=> 31 (P0.31)
// <32=> 32 (P1.0)
// <33=> 33 (P1.1)
// <34=> 34 (P1.2)
// <35=> 35 (P1.3)
// <36=> 36 (P1.4)
// <37=> 37 (P1.5)
// <38=> 38 (P1.6)
// <39=> 39 (P1.7)
// <40=> 40 (P1.8)
// <41=> 41 (P1.9)
// <42=> 42 (P1.10)
// <43=> 43 (P1.11)
// <44=> 44 (P1.12)
// <45=> 45 (P1.13)
// <46=> 46 (P1.14)
// <47=> 47 (P1.15)
#define MATRIX_ROW_PINS \
{ 26, 29, 2, 45, 43}
#define MATRIX_COL_PINS \
{ 41, 28, 12, 42, 7, 3, 38, 36, 34, 32, 24, 22, 13, 20 }

#define IGNORE_ATOMIC_BLOCK
/* COL2ROW, ROW2COL*/
#define DIODE_DIRECTION COL2ROW

#ifdef RGBLIGHT_ENABLE
	#define RGB_DI_PIN 6
	#define RGBLED_NUM 20
	#define RGBLIGHT_ANIMATIONS
#endif

#define DEBOUNCE 20
#define BLUETOOTH_ENABLE


#define PERMISSVIE_HOLD
#define IGNORE_MOD_TAP_INTERRUPT
#define TAPPING_FORCE_HOLD
#define TAPPING_TERM 150
#define ONESHOT_TIMEOUT 120

#ifdef RGB_MATRIX_ENABLE
	#define RGB_MATRIX_KEYPRESSES
	#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
	#define RGB_MATRIX_LED_PROCESS_LIMIT 20
	#define RGB_MATRIX_LED_FLUSH_LIMIT 26
	#define DRIVER_ADDR_1 0b1010000
	#define DRIVER_ADDR_2 0b1011111

	#define DRIVER_COUNT 2
	#define DRIVER_1_LED_TOTAL 47
	#define DRIVER_2_LED_TOTAL 36
	#define DRIVER_LED_TOTAL DRIVER_1_LED_TOTAL + DRIVER_2_LED_TOTAL

	// #define DISABLE_RGB_MATRIX_ALPHAS_MODS
	// #define DISABLE_RGB_MATRIX_GRADIENT_UP_DOWN
	//#define DISABLE_RGB_MATRIX_BREATHING
	//#define DISABLE_RGB_MATRIX_BAND_SAT
	//#define DISABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
	//#define DISABLE_RGB_MATRIX_BAND_SPIRAL_SAT
	//#define DISABLE_RGB_MATRIX_RAINDROPS
	//#define DISABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
	//#define DISABLE_RGB_MATRIX_TYPING_HEATMAP
	//#define DISABLE_RGB_MATRIX_DIGITAL_RAIN
	//#define DISABLE_RGB_MATRIX_SOLID_REACTIVE
	//#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
	//#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
	//#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
	//#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
	//#define DISABLE_RGB_MATRIX_SPLASH
	//#define DISABLE_RGB_MATRIX_SOLID_SPLASH
#endif

#define KEYBOARD_SCAN_INTERVAL 6
// The pin number for SCL pin
#define I2C1_SCL 15
// The pin number for SDA pin
#define I2C1_SDA 17
// Enter sleep modes after 600 seconds
#define POWER_SAVE_TIMEOUT 2000
// #define BLE_NKRO
// Force NKRO
// #define FORCE_NKRO
#define NKRO_EPSIZE 22
// Enable watchdog
// #define KBD_WDT_ENABLE
#define MAX_ENDPOINTS 8

/*
 * MIDI options
 */

/* Prevent use of disabled MIDI features in the keymap */
//#define MIDI_ENABLE_STRICT 1

/* enable basic MIDI features:
   - MIDI notes can be sent when in Music mode is on
*/

//#define MIDI_BASIC

/* enable advanced MIDI features:
   - MIDI notes can be added to the keymap
   - Octave shift and transpose
   - Virtual sustain, portamento, and modulation wheel
   - etc.
*/
//#define MIDI_ADVANCED

/* override number of MIDI tone keycodes (each octave adds 12 keycodes and allocates 12 bytes) */
//#define MIDI_TONE_KEYCODE_OCTAVES 6

#endif
