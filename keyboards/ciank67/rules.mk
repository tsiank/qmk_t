MCU = NRF52840
SRC += keyboards/ciank67/lighting.c
SRC += keyboards/ciank67/underglow_test.c
SRC += keyboards/ciank67/midi_typing.c

MUSIC_STREAM_ENABLE ?= yes
ifeq ($(strip $(MUSIC_STREAM_ENABLE)),yes)
    OPT_DEFS += -DMUSIC_STREAM_ENABLE
    RGB_MATRIX_CUSTOM_KB = yes
    SRC += keyboards/ciank67/music.c
endif

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

ifeq ($(strip $(MUSIC_STREAM_ENABLE)),yes)
ifeq ($(strip $(AUDIO_ENABLE)),yes)
ifeq ($(strip $(MIDI_ENABLE)),yes)
    OPT_DEFS += -DKEYBOARD_CONTROL_ENABLE
    SRC += keyboards/ciank67/keyboard_control.c
endif
endif
endif
