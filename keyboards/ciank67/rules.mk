MCU = NRF52840
# Bootloader selection
BOOTLOADER = custom

# GCC ARM Toolchain path
GNU_INSTALL_ROOT =
GNU_PREFIX = arm-none-eabi

MOUSEKEY_ENABLE = yes
BOOTMAGIC_ENABLE = yes
AUDIO_ENABLE = yes
AUDIO_DRIVER = pwm_hardware
MIDI_ENABLE = yes
EXTRAKEY_ENABLE = yes
NKRO_ENABLE = yes
RGB_MATRIX_ENABLE = yes
RGB_MATRIX_DRIVER = is31fl3741
RGBLIGHT_ENABLE = yes
# WS2812_DRIVER_REQUIRED = yes
WS2812_DRIVER = pwm
# NRF_DEBUG = yes
DEBOUNCE_TYPE = sym_eager_pr

# Shared nRF5 client features; no per-keyboard source copies.
NRF5_CONFIG_ENABLE = yes
MUSIC_STREAM_ENABLE ?= yes
