# Qmknrf5 shared features

Set `NRF5_CONFIG_ENABLE = yes` and (for the music/control transports)
`MUSIC_STREAM_ENABLE = yes` in an nRF5 keyboard's rules.mk. The build system
loads rules.mk here after all keyboard/keymap overrides. Periodic tasks run
from the nRF5 main loop; no keymap housekeeping hook is required.

Keep hardware configuration, matrix and LED maps, indicator segments and
keymaps in keyboards/<name>. Reuse nrf5_lighting.h and nrf5_midi_typing.h from
process_record_user for keymap-specific custom toggle codes. The shared
renderer reads QMK's g_led_config; it does not contain ciank67 LED indices.

See util/ciank67_music/WinUI/README.md for the full integration and layout
profile format, including optional RGB effects and the VIA matrix query.

New keyboard profiles can define `KEYBOARD_CONTROL_CAPABILITIES` in config.h
to report state v2 with the actual available lights, audio and MIDI modes.
The updated client accepts both v1 (legacy ciank67) and v2. This avoids exposing
absent matrix/audio controls on an underglow-only pad.

## Shared OLED and encoder UI

The optional UI modules live here, independently of any keyboard matrix or
keymap. They can be used without `NRF5_CONFIG_ENABLE` or music streaming:

```make
OLED_ENABLE = yes
OLED_DRIVER = ssd1306
NRF5_OLED_STATUS_ENABLE = yes
ENCODER_ENABLE = yes
NRF5_ENCODER_ENABLE = yes
```

In the keyboard's config.h, set the actual bus/pins and UI parameters:

```c
#define I2C1_SCL 17
#define I2C1_SDA 15
#define OLED_DISPLAY_128X32
#define OLED_DISPLAY_ADDRESS 0x3C
#define OLED_UPDATE_INTERVAL 100
#define OLED_I2C_TIMEOUT 100
#define ENCODER_A_PINS {24}
#define ENCODER_B_PINS {22}
#define ENCODER_RESOLUTION 2
#define TAP_CODE_DELAY 20
#define NRF5_OLED_TITLE "My keyboard"
#define NRF5_OLED_LAYER_NAMES {"BASE", "NAV", "RGB"}
#define NRF5_ENCODER_BINDINGS { \
    {0, 0, NRF5_ENCODER_KEYS, KC_VOLU, KC_VOLD}, \
    {0, 1, NRF5_ENCODER_KEYS, KC_PGUP, KC_PGDN}, \
    {0, 2, NRF5_ENCODER_UNDERGLOW, KC_NO, KC_NO}, \
}
```

Layer numbers are the keyboard's layer indices. Multiple encoders are supported;
set `index` and add rows for each encoder/layer. `NRF5_ENCODER_KEYS` accepts
16-bit QMK keycodes (including modifiers), or KC_NO to disable one direction.
A recognized encoder does nothing on layers without a binding; unknown indices
return true. The highest active/default layer selects the binding, with no
implicit fallback to lower layers. The default binding is encoder 0, layer 0,
clockwise volume up and counterclockwise volume down.

`NRF5_ENCODER_UNDERGLOW` adjusts hue, Ctrl adjusts saturation and Alt adjusts
brightness; clockwise decreases and counterclockwise increases, preserving the
pad's existing convention. `NRF5_ENCODER_MATRIX` provides the same controls for
RGB Matrix. Enable the corresponding QMK lighting feature when using these
binding types. Pin direction flip and resolution still use standard QMK options.
Do not enable ENCODER_MAP_ENABLE with this callback-based module.

Only these thin callbacks belong in the keyboard's C file:

```c
#include "nrf5_ui.h"
bool encoder_update_kb(uint8_t index, bool clockwise) {
    return nrf5_encoder_update(index, clockwise);
}
bool oled_task_kb(void) {
    return nrf5_oled_task();
}
```

Include only callbacks for enabled modules. Each shared callback invokes its
QMK `_user` callback first. Returning false from the user callback replaces the
shared handling, allowing custom screens/icons without modifying this library.
The default OLED page shows title/layer/USB or BLE, battery measurement, lighting,
Num/Caps and MIDI typing. Its four-row page is intended for 128x32 or larger
screens. RGBLIGHT is shown when both lighting types exist; otherwise RGB Matrix
is shown. Missing lighting/MIDI modules are supported. Keep short ASCII titles
and layer names; unmapped layers display their number. Text is clipped to the
screen's character width.

### Chinese text and icons

The monochrome OLED is a pixel display. It can show Chinese glyphs and icons,
but QMK's default font/string renderer does not decode arbitrary UTF-8 Chinese.
Use selected pre-rendered glyphs/bitmaps, a custom font mapping, or your own text
renderer. Prefer storing only the characters actually needed rather than a full
Chinese font. A 16x16 monochrome glyph uses 32 bytes; a 128x32 frame uses 512 bytes.
At 16x16, the screen fits 8 Chinese glyphs per row and 2 rows.

`oled_write_raw_P()` / `oled_write_raw_byte()` accept SSD1306 page-format pixels;
each byte represents eight vertical pixels. `oled_write_pixel()` also allows
conversion from other bitmap formats. A custom `oled_task_user()` may draw these
and return false to replace the standard status page. This change adds the shared
status renderer, not a UTF-8 font engine or a Chinese font asset.

### Sharing I2C with RGB Matrix

Use the same physical SCL/SDA pins for the shared bus and distinct **7-bit**
addresses. OLED normally uses 0x3C; ciank67's IS31FL3741 uses 0x30, so these
addresses do not collide. Check each additional device's configuration.

Both QMK OLED and matrix updates run in the main loop, and the nRF transport
waits for each EasyDMA transaction. Do not call these drivers from interrupts.
Repeated i2c_init() calls (including OLED retry) reuse the initialized TWIM and
do not rerun the matrix wake sequence. Failed initialization remains retryable.
With OLED_ENABLE, i2c_stop() still sends the legacy LED shutdown commands but
keeps TWIM available for the display; i2c_start() still wakes the LED chip.
Without OLED, the previous peripheral shutdown behavior is retained. Legacy
IS31FL3741 wake/shutdown commands both select its function page 0x04 (page 0x03
is a scaling page, not the configuration page).

OLED and matrix share bandwidth, so avoid continuously refreshing a full frame;
use dirty updates and a moderate OLED_UPDATE_INTERVAL such as 100 ms. Ensure
pull-ups/voltage and the selected bus frequency suit all connected devices.

### Optional 16-pixel bitmap title

Define `NRF5_OLED_TITLE_BITMAP_HEADER "oled_title.h"` in a keyboard's config.h
to replace the ASCII title with a custom monochrome title. Keep the asset header
in that keyboard's directory; other keyboards do not inherit its artwork.
The header provides:

```c
#define NRF5_OLED_TITLE_BITMAP_WIDTH 64
#define NRF5_OLED_TITLE_BITMAP_HEIGHT 16
static const uint8_t nrf5_oled_title_bitmap[] = { /* 128 bytes */ };
```

Use row-major, MSB-left data: each row has `(width + 7) / 8` bytes, progressing
from top to bottom. The shared renderer converts these pixels to QMK/SSD1306
vertical page bytes. Width must leave at least six complete text columns to the
right; height is 16 pixels and the display must be at least 32 pixels tall.
Layer and connection labels occupy the right-hand side of the title; lighting
and lock/MIDI state occupy the following two text rows. The bitmap layout omits
the battery row. Removing the macro restores the original four-row ASCII page.

The pad's example is the user-supplied PCtoLCD `zimo.TXT`: four 16x16 新宋体
glyphs, 紫雪蓝海, combined horizontally into 64x16 pixels (128 bytes).

## Traditional clock (optional)

Set `NRF5_CLOCK_ENABLE = yes` with `NRF5_CONFIG_ENABLE = yes` and
`MUSIC_STREAM_ENABLE = yes`. With the shared OLED module, the lower 16 pixels
show traditional time and replace the light/lock status rows. The bitmap title
remains above; its right-hand labels show HH:MM and layer/connection.

One branch spans two hours, starting at an odd hour: 23:00 子, 01:00 丑,
03:00 寅, 05:00 卯, 07:00 辰, 09:00 巳, 11:00 午, 13:00 未,
15:00 申, 17:00 酉, 19:00 戌, 21:00 亥. The first hour is 初 and the
second is 正. Within each hour, minutes 00–14 are 初刻, 15–29 一刻,
30–44 二刻, 45–59 三刻. Examples:

| Local time | Display |
| --- | --- |
| 11:45 | 午初三刻 |
| 12:00 | 午正初刻 |
| 23:00 | 子初初刻 |
| 00:00 | 子正初刻 |

The updated client synchronizes Windows local wall time after connecting over
USB or BLE, then once a minute while control remains connected. Time sync is a
separate command; it does not start music, switch lights/MIDI, write EEPROM or
extend the battery sleep timeout. Closing the client leaves RTC time advancing
while the keyboard remains powered/awake. Power loss or System OFF reset loses
time; before a new sync, the display reads --:-- and 待校时. Existing OLED idle
timeout/wake behavior is retained. The clock uses the 32768 Hz app_timer RTC
(prescaler 0), with fractional ticks and rollover handling.

Protocol: a 20-byte music/control packet, C7 05 sequence hour minute second
millisecond-low millisecond-high followed by 12 zero bytes. The firmware validates
ranges and queues application in the main loop. Reply C7 83 uses state version
3: v2 capability fields remain at bytes 6–7, byte 8 is clock capability 1,
byte 9 is synchronized 0/1, bytes 10–19 are zero. Non-clock firmware continues
reporting its existing v1/v2 version. Use the new client with v3 firmware; the
new client also supports older v1/v2 keyboards and never sends clock commands
to them. No USB/GATT descriptor or service UUID changes are needed.

The finite 16x16 clock glyph subset comes from GNU Unifont 15.1.05 (672 bytes),
under SIL OFL 1.1; see UNIFONT-LICENSE.txt. The user-provided title asset remains
separate in the keyboard directory.

## R8 display layout and power policy

On the 128x32 clock page, the top-left title is retained. The top-right shows
connection/battery percentage and layer index/name. The lower 16 pixels show
traditional time at x=0 followed by a doubled 12x16 numeric HH:MM at x=68.
The clock page now requires at least 128x32. Battery percentage uses the existing
ADC estimate; the firmware cannot reliably distinguish an absent battery from a
charging/open battery input. The display does not claim a measured USB power level. Set
NRF5_OLED_BATTERY_AVAILABLE to 0 for USB-only/no-battery hardware: the clock
page shows --% and does not read the battery getter. The current test pad uses
this option; enable percentage only after configuring/calibrating the actual
battery ADC circuit (its schematic routes BAT_EG to AIN2, while the inherited
nRF ADC default is AIN3, which is a matrix row on this pad).

The shared OLED module also includes oled_power.c. Its mode defaults to automatic
on reset. Automatic keeps the display on while USB VBUS/USBD is present; without
USB it follows physical input activity and OLED_TIMEOUT (pad: 60000 ms). Always
on bypasses display idle timeout; off blocks all key/encoder/render wake requests.
The choice lives in RAM until reset and does not change keyboard System OFF rules.
Clock sync and periodic redraws do not count as physical input. Idle state is
latched until actual input, avoiding false wakes at the short QMK timer rollover.
The nRF main loop applies policy after display work. Driver wake/render guards
are compiled only for NRF5_OLED_STATUS_ENABLE, leaving other platforms unchanged.

Client control: C7 06 sequence mode, with mode 0 automatic / 1 always-on / 2 off,
followed by 16 zero bytes. Reply remains state v3; byte 8 is now a feature mask
(bit 0 clock, bit 1 screen control). Byte 9 is clock synchronized when supported;
byte 10 is display mode and byte 11 is actual screen on/off. Unsupported feature
fields must remain zero; bytes 12–19 are reserved zero. The new client retains
v1/v2 and clock-only v3 compatibility, disables the screen selector on older
firmware, and validates acknowledgements before applying the selected mode.

## R9 compact clock metadata / larger layer label

The clock page keeps the user title at top-left and draws the layer name at
x=68, y=0 in 12x16 text (up to five printable ASCII characters). An unmapped
layer uses L<number>. The clock title bitmap must be at most 64 pixels wide.
Traditional time stays at x=0, y=16. The bottom-right now has two **6x8** rows:
HH:MM plus battery percentage, then connection plus L<number>. In no-battery
configuration this is `11:45 --%` and `USB L0`, for example. Numeric time is no
longer doubled. The printable ASCII bitmap subset comes from QMK's glcdfont.c.
Each strip is composed before writing raw OLED bytes, so shorter layer names
clear previous letters and no text wrapping can erase the title or clock.
Power policy, synchronization and wire protocol are unchanged from R8.

## Optional logo before the bitmap title (R10)

A keyboard can define `NRF5_OLED_TITLE_PREFIX_BITMAP_HEADER "oled_logo.h"` alongside
its title header. That header supplies `NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH`,
`NRF5_OLED_TITLE_PREFIX_BITMAP_HEIGHT` (16), and the row-major/MSB-left byte array
`nrf5_oled_title_prefix_bitmap`. Prefix pixels are drawn before the unmodified
name glyphs. Combined title width is checked at compile time. On the clock page,
the large layer label starts after the combined artwork (at least x=68), and
space for four 12x16 characters is required. Lower clock/status positions do not
move. Without the prefix macro, previous title placement is preserved.

The pad example uses the red emblem from the user's supplied logo.png. Only the
red artwork is extracted; the source caption is excluded. A 16x9 rendition keeps
the emblem's proportions and is vertically centered in a 16x16 prefix cell,
requiring 32 flash bytes. With its 64-pixel name, the combined title is 80 pixels
and the large layer label starts at x=80. The original image and its SHA256 are
kept alongside the generated header for reference.
