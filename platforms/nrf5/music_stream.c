// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "music_stream.h"
#include <string.h>
#include "app_util_platform.h"
#include "timer.h"

static music_frame_t latest;
static uint32_t received_at;
static bool stream_valid;
static struct { bool valid; music_rgb_t value; } colors[3];

bool music_stream_receive(const uint8_t *data, size_t length, music_source_t source) {
    if (!data || length != MUSIC_PACKET_SIZE || data[0] != 0xC7 ||
        (source != MUSIC_SOURCE_USB && source != MUSIC_SOURCE_BLE && source != MUSIC_SOURCE_ESB)) return false;
#ifdef KEYBOARD_CONTROL_ENABLE
    if (data[1] == 3 || data[1] == 4 || data[1] == 5 || data[1] == 6) {
        extern bool keyboard_control_receive(const uint8_t *, music_source_t);
        return keyboard_control_receive(data, source);
    }
#endif
    uint8_t index = source - 1;
    if (data[1] == 2) {
        for (uint8_t i = 5; i < MUSIC_PACKET_SIZE; ++i) if (data[i]) return false;
        CRITICAL_REGION_ENTER();
        colors[index].valid = true;
        colors[index].value = (music_rgb_t){data[2], data[3], data[4]};
        if (latest.source == source) {
            latest.custom_color = true;
            latest.color = colors[index].value;
        }
        CRITICAL_REGION_EXIT();
        // Choosing a color never starts music or extends the audio timeout.
        return true;
    }
    if (data[1] != 1 || (data[3] & ~(MUSIC_TARGET_MASK | MUSIC_EFFECT_MASK | MUSIC_MONOCHROME))) return false;
    uint32_t now = timer_read32();
    // USB writes are dispatched in the main loop; BLE events can interrupt it.
    // Keep only the newest frame and never render or touch flash in a callback.
    CRITICAL_REGION_ENTER();
    latest.sequence = data[2];
    latest.flags = data[3] & MUSIC_TARGET_MASK;
    latest.effect = (data[3] & MUSIC_EFFECT_MASK) >> MUSIC_EFFECT_SHIFT;
    latest.monochrome = (data[3] & MUSIC_MONOCHROME) != 0;
    if (!latest.flags) colors[index].valid = false;
    latest.custom_color = colors[index].valid;
    latest.color = colors[index].value;
    memcpy(latest.bands, data + 4, MUSIC_BANDS);
    latest.source = source;
    received_at = now;
    stream_valid = latest.flags != 0;
    CRITICAL_REGION_EXIT();
    return true;
}

bool music_stream_read(music_frame_t *frame) {
    bool valid;
    CRITICAL_REGION_ENTER();
    // Latch expiration: nRF's short RTC wrap must not resurrect an old frame
    // and wake idle lighting. Check and clear under the same lock as reception.
    if (stream_valid && timer_elapsed32(received_at) >= MUSIC_TIMEOUT_MS) stream_valid = false;
    *frame = latest;
    valid = stream_valid;
    CRITICAL_REGION_EXIT();
    return valid;
}

void music_stream_disconnect(music_source_t source) {
    if (source != MUSIC_SOURCE_USB && source != MUSIC_SOURCE_BLE && source != MUSIC_SOURCE_ESB) return;
    CRITICAL_REGION_ENTER();
    colors[source - 1].valid = false;
    if (latest.source == source) { latest.flags = 0; stream_valid = false; }
    CRITICAL_REGION_EXIT();
}
