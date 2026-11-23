#pragma once
#define CONFIG_H

// This board has a wired battery divider; opt in rather than sampling floating pins.
#define NRF5_BATTERY_ENABLE

/* Bluetooth connection setting*/
#define BLE_HID_MIN_INTERVAL 7.5    /**UNIT_1_25_MS< Minimum connection interval (7.5 ms) */
//#define BLE_HID_MAX_INTERVAL 30   /**UNIT_1_25_MS< Maximum connection interval (30 ms). */
//#define BLE_HID_SLAVE_LATENCY 6  /**< Slave latency. */
#define BLE_HID_MAX_INTERVAL 20  /**UNIT_1_25_MS< Maximum connection interval (30 ms). */  //Decreasing the maximum communication interval (ms) with the terminal reduces the delay between the terminal and the master, but increases power consumption. The default is 60 (90 before 2020/07/27)
#define BLE_HID_SLAVE_LATENCY 3  /**< Slave latency. */  //If you lower the communication parameters with the terminal, the communication frequency will increase, but the power consumption will increase. Should it be inversely proportional to HID_INTERVAL? The default is 3 (4 before 2020/07/27)
#define BLE_HID_TIMEOUT 430       /**UNIT_10_MS< Connection supervisory timeout (430 ms). */

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
{ 41, 28, 12, 42, 7, 3, 20, 13, 22, 24, 32, 34,36,38}

#define IGNORE_ATOMIC_BLOCK
/* COL2ROW, ROW2COL*/
#define DIODE_DIRECTION COL2ROW

#ifdef RGBLIGHT_ENABLE
	#define WS2812_DI_PIN 6
    // Actual 16 MHz PWM pulse lengths used by qmk_tsiank_stable: 6/12 ticks.
    #define WS2812_T0H 375
    #define WS2812_T1H 750
	#define RGBLIGHT_EFFECT_ALTERNATING
	#define RGBLIGHT_EFFECT_BREATHING
	#define RGBLIGHT_EFFECT_CHRISTMAS
	#define RGBLIGHT_EFFECT_KNIGHT
	#define RGBLIGHT_EFFECT_RAINBOW_MOOD
	#define RGBLIGHT_EFFECT_RAINBOW_SWIRL
	#define RGBLIGHT_EFFECT_RGB_TEST
	#define RGBLIGHT_EFFECT_SNAKE
	#define RGBLIGHT_EFFECT_STATIC_GRADIENT
	#define RGBLIGHT_EFFECT_TWINKLE
	#define RGBLIGHT_LAYERS
	#define RGBLIGHT_LAYER_BLINK

	#define RGB_PWR_PIN 8 // P-mos
//#define RGB_PWR_PIN_REVERSE 20 // N-mos
#endif

#define NRF5_UNDERGLOW_TEST_WHITE_COUNT 8

#define DEBOUNCE 20

#define PERMISSVIE_HOLD
#define HOLD_ON_OTHER_KEY_PRESS
#define QUICK_TAP_TERM 0
#define TAPPING_TERM 150
#define ONESHOT_TIMEOUT 120

#ifdef RGB_MATRIX_ENABLE
    #define RGB_MATRIX_DISABLE_SHARED_KEYCODES
	#define RGB_MATRIX_KEYPRESSES
	#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
	//#define RGB_DISABLE_WHEN_USB_SUSPENDED

	#define RGB_MATRIX_LED_PROCESS_LIMIT 20
	#define RGB_MATRIX_LED_FLUSH_LIMIT 26

// RGB Matrix Animation modes. Explicitly enabled
	#define ENABLE_RGB_MATRIX_ALPHAS_MODS
	#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
	#define ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
	#define ENABLE_RGB_MATRIX_BREATHING
	#define ENABLE_RGB_MATRIX_BAND_SAT
	#define ENABLE_RGB_MATRIX_BAND_VAL
	#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
	#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
	#define ENABLE_RGB_MATRIX_BAND_SPIRAL_SAT
	#define ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
	#define ENABLE_RGB_MATRIX_CYCLE_ALL
	#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
	#define ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
	#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
	#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN
	#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
	#define ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
	#define ENABLE_RGB_MATRIX_CYCLE_SPIRAL
	#define ENABLE_RGB_MATRIX_DUAL_BEACON
	#define ENABLE_RGB_MATRIX_RAINBOW_BEACON
	#define ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
	#define ENABLE_RGB_MATRIX_RAINDROPS
	#define ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
	#define ENABLE_RGB_MATRIX_HUE_BREATHING
	#define ENABLE_RGB_MATRIX_HUE_PENDULUM
	#define ENABLE_RGB_MATRIX_HUE_WAVE
	#define DISABLE_RGB_MATRIX_FRACTAL
	#define ENABLE_RGB_MATRIX_TYPING_HEATMAP
	#define ENABLE_RGB_MATRIX_DIGITAL_RAIN
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
	#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
	#define ENABLE_RGB_MATRIX_SPLASH
	#define ENABLE_RGB_MATRIX_MULTISPLASH
	#define ENABLE_RGB_MATRIX_SOLID_SPLASH
	#define ENABLE_RGB_MATRIX_SOLID_MULTISPLASH

	#define IS31FL3741_I2C_ADDRESS_1 0x30
	// Used by the nRF5 I2C power control.
	#define DRIVER_ADDR_1 IS31FL3741_I2C_ADDRESS_1
	#define RGB_MATRIX_LED_COUNT 67

#endif

#define KEYBOARD_SCAN_INTERVAL 6
// The pin number for SCL pin
#define I2C1_SCL 15
// The pin number for SDA pin
#define I2C1_SDA 17
// Enter sleep modes after 600 seconds
#define POWER_SAVE_TIMEOUT 2000
//#define BLE_NKRO
// Force NKRO
// #define FORCE_NKRO
#define NKRO_EPSIZE 22
// Enable watchdog
// #define KBD_WDT_ENABLE

//#define WPM_ENABLE

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

// LS1: buzzer1 = E73 pad 9 / P0.31; buzzer2 = pad 10 / P0.30.
#define AUDIO_PIN 31
#define AUDIO_PIN_ALT 30
#define AUDIO_INIT_DELAY
#define AUDIO_CLICKY
#define AUDIO_DEFAULT_CLICKY_ON false
#define MIDI_BASIC
#define MIDI_ADVANCED

#ifdef VIA_ENABLE
// Preserve all seven default layers and leave room for VIA macros.
#    define DYNAMIC_KEYMAP_LAYER_COUNT 7
#    define EEPROM_SIZE 2048
// Keep large VIA/RGB configuration saves in short SoftDevice operations,
// as on ciank_pad, rather than submitting the whole 2 KiB in one write.
#    define NRF_FSTORAGE_SD_MAX_WRITE_SIZE 128
#endif

// Wireless standby uses low-power PORT sensing; active typing keeps 6 ms scans.
#define NRF5_MATRIX_IDLE_ENABLE
#define GPIOTE_CONFIG_NUM_OF_LOW_POWER_EVENTS MATRIX_COLS
#define NRFX_GPIOTE_CONFIG_NUM_OF_LOW_POWER_EVENTS MATRIX_COLS
