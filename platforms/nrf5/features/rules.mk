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

# UI modules also work without music/client features.
ifeq ($(strip $(NRF5_OLED_STATUS_ENABLE)),yes)
    ifneq ($(strip $(OLED_ENABLE)),yes)
        $(error NRF5_OLED_STATUS_ENABLE requires OLED_ENABLE = yes)
    endif
    COMMON_VPATH += platforms/nrf5/features
    OPT_DEFS += -DNRF5_OLED_STATUS_ENABLE
    SRC += platforms/nrf5/features/oled_status.c platforms/nrf5/features/oled_power.c
endif
ifeq ($(strip $(NRF5_ENCODER_ENABLE)),yes)
    ifneq ($(strip $(ENCODER_ENABLE)),yes)
        $(error NRF5_ENCODER_ENABLE requires ENCODER_ENABLE = yes)
    endif
    ifeq ($(strip $(ENCODER_MAP_ENABLE)),yes)
        $(error NRF5_ENCODER_ENABLE uses encoder_update_kb; disable ENCODER_MAP_ENABLE)
    endif
    COMMON_VPATH += platforms/nrf5/features
    OPT_DEFS += -DNRF5_ENCODER_ENABLE
    SRC += platforms/nrf5/features/encoder.c
endif

ifeq ($(strip $(NRF5_CLOCK_ENABLE)),yes)
    ifneq ($(strip $(NRF5_CONFIG_ENABLE)),yes)
        $(error NRF5_CLOCK_ENABLE requires NRF5_CONFIG_ENABLE = yes)
    endif
    ifneq ($(strip $(MUSIC_STREAM_ENABLE)),yes)
        $(error NRF5_CLOCK_ENABLE requires MUSIC_STREAM_ENABLE = yes for host time sync)
    endif
    OPT_DEFS += -DNRF5_CLOCK_ENABLE
    SRC += platforms/nrf5/features/clock.c
endif
