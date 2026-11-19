// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_PACKET_SIZE 20
#define MUSIC_BANDS 16
#define MUSIC_MATRIX 1
#define MUSIC_UNDERGLOW 2
#define MUSIC_TIMEOUT_MS 750
#define MUSIC_USB_REPORT_ID 4

typedef enum { MUSIC_SOURCE_USB = 1, MUSIC_SOURCE_BLE = 2 } music_source_t;
typedef struct {
    uint8_t sequence;
    uint8_t flags;
    uint8_t bands[MUSIC_BANDS];
    music_source_t source;
} music_frame_t;

bool music_stream_receive(const uint8_t *data, size_t length, music_source_t source);
bool music_stream_read(music_frame_t *frame);
void music_stream_disconnect(music_source_t source);
