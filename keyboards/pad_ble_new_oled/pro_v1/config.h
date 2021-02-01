#ifndef MASTER_CONFIG_H_
#define MASTER_CONFIG_H_

#define MATRIX_ROW_PINS \
{ 41, 5, 43, 38, 36}
#define MATRIX_COL_PINS \
{ 29, 45, 9, 10, 13 }

#define I2C1_SCL 15
// The pin number for SDA pin
#define I2C1_SDA 17

#define POWER_PIN 8
#define LED_PIN 10
#define SWITCH_PIN 12

#define ENCODERS_PAD_A { 24 }
#define ENCODERS_PAD_B { 22 }
#define TAP_CODE_DELAY 20
#define ENCODER_DIRECTION_FLIP
#define ENCODER_RESOLUTION 2

#define TAPPING_TERM 200
// #define SSD1306OLED
// #define OLED_DRIVER_ENABLE

// #define OLED_DISABLE_TIMEOUT 0
#define OLED_FONT_H "keyboards/pad_ble_new_oled/lib/glcdfont.c"

#define IS_LEFT_HAND  true
#define ENABLE_STARTUP_ADV_NOLIST

#endif /* MASTER_CONFIG_H_ */
