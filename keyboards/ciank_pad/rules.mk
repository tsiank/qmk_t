MCU = NRF52840

# GCC ARM Toolchain path
GNU_INSTALL_ROOT =
GNU_PREFIX = arm-none-eabi

MOUSEKEY_ENABLE = yes
BOOTMAGIC_ENABLE = full
# MIDI_ENABLE = yes
EXTRAKEY_ENABLE = yes
NKRO_ENABLE = yes
# RGB_MATRIX_ENABLE = IS31FL3737
RGBLIGHT_ENABLE = yes
# NRF_DEBUG = yes
DEBOUNCE_TYPE = eager_pr
ENCODER_ENABLE = yes
OLED_DRIVER_ENABLE = yes
# If you want to change the display of OLED, you need to change here
SRC +=  ./lib/rgb_state_reader.c \
        ./lib/layer_state_reader.c \
        ./lib/logo_reader.c \
        ./lib/keylogger.c \
        # ./lib/mode_icon_reader.c \
        # ./lib/host_led_state_reader.c \
        ./lib/timelogger.c \