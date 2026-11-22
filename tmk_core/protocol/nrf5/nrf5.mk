PROTOCOL_DIR = protocol
NRF_DIR = $(PROTOCOL_DIR)/nrf5

ifeq ($(strip $(NRF5_ESB_DONGLE)),yes)
# Reuse the board's build/USB identity, not its keyboard implementation.
SRC := $(PLATFORM_COMMON_DIR)/timer.c \
       $(PLATFORM_COMMON_DIR)/bootloaders/$(BOOTLOADER_TYPE).c \
       platforms/nrf5/dongle/main.c \
       platforms/nrf5/dongle/usb.c \
       $(NRF_DIR)/usb/midi_usb.c \
       $(NRF_DIR)/usb/fixed/app_usbd_hid_generic.c
else
SRC += \
	$(NRF_DIR)/adc.c \
	$(NRF_DIR)/outputselect.c \
	$(NRF_DIR)/main.c \
	$(NRF_DIR)/outputselect.c \
	$(NRF_DIR)/usb/nrf5_usb.c \
	$(NRF_DIR)/usb/fixed/app_usbd_hid_generic.c \
	$(NRF_DIR)/usb/fixed/app_usbd_audio.c \
	$(NRF_DIR)/ble/ble_service.c \
	$(NRF_DIR)/ble/ble_hids_service.c \
	$(NRF_DIR)/ble/ble_midi.c \

endif

VPATH += $(TMK_PATH)/$(PROTOCOL_DIR)
VPATH += $(TMK_PATH)/$(NRF_DIR)
VPATH += $(TMK_PATH)/$(NRF_DIR)/ble
VPATH += $(TMK_PATH)/$(NRF_DIR)/usb
VPATH += $(TMK_PATH)/$(NRF_DIR)/lufa_utils
