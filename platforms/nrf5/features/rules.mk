# Common Qmknrf5 configuration/music features. Include after the keyboard's
# RGB/audio/MIDI settings, so QMK discovers custom effects before compiling.
ifeq ($(strip $(NRF5_CONFIG_ENABLE)),yes)
    OPT_DEFS += -DNRF5_CONFIG_ENABLE
    COMMON_VPATH += platforms/nrf5/features
    SRC += platforms/nrf5/features/features.c \
           platforms/nrf5/features/lighting.c \
           platforms/nrf5/features/midi_typing.c \
           platforms/nrf5/features/underglow_test.c
    ifeq ($(strip $(MUSIC_STREAM_ENABLE)),yes)
        OPT_DEFS += -DMUSIC_STREAM_ENABLE -DKEYBOARD_CONTROL_ENABLE
        SRC += platforms/nrf5/features/music.c platforms/nrf5/features/keyboard_control.c
        ifeq ($(strip $(RGB_MATRIX_ENABLE)),yes)
            RGB_MATRIX_CUSTOM_KB = yes
        endif
    endif
endif
