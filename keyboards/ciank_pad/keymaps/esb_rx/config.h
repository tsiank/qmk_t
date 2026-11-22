// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#undef PRODUCT
#define PRODUCT "Qmknrf5 2.4G Receiver"
#undef PRODUCT_ID
#define PRODUCT_ID 0x8A43
// USB descriptor revision: adds the AudioControl/MIDIStreaming interfaces.
#define APP_USBD_DEVICE_VER_MINOR 1
#undef NKRO_EPSIZE
#define NKRO_EPSIZE 31
// Keep the pad's existing Flash record area when replacing its full firmware.
#define FDS_VIRTUAL_PAGES 5
#define NRF_FSTORAGE_SD_MAX_WRITE_SIZE 128
