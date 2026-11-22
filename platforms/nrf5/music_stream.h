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
#define MUSIC_TARGET_MASK (MUSIC_MATRIX | MUSIC_UNDERGLOW)
#define MUSIC_EFFECT_SHIFT 4
#define MUSIC_EFFECT_MASK 0x10
#define MUSIC_MONOCHROME 0x20
#define MUSIC_TIMEOUT_MS 750
#define MUSIC_USB_REPORT_ID 4

typedef enum { MUSIC_SOURCE_USB = 1, MUSIC_SOURCE_BLE = 2, MUSIC_SOURCE_ESB = 3 } music_source_t;
typedef enum { MUSIC_EFFECT_SPECTRUM = 0, MUSIC_EFFECT_DUAL_DANCE = 1 } music_effect_t;
typedef struct { uint8_t r, g, b; } music_rgb_t;
typedef struct {
    uint8_t sequence;
    uint8_t flags;
    music_effect_t effect;
    bool monochrome;
    bool custom_color;
    music_rgb_t color;
    uint8_t bands[MUSIC_BANDS];
    music_source_t source;
} music_frame_t;

bool music_stream_receive(const uint8_t *data, size_t length, music_source_t source);
bool music_stream_read(music_frame_t *frame);
void music_stream_disconnect(music_source_t source);
