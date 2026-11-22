// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_lighting.h"
#include "nrf5_esb.h"
#include "esb_pair.h"
#include "rgblight.h"
#ifdef RGBLIGHT_LAYERS

// Append a temporary highest-priority layer. Do not change enable/HSV/mode,
// write EEPROM, or replace the keyboard's Caps Lock/layer state.
static const rgblight_segment_t *overlay_layers[RGBLIGHT_MAX_LAYERS + 1];
static rgblight_segment_t pixels[] = {{0, RGBLIGHT_LED_COUNT, 0, 255, 96}, RGBLIGHT_END_SEGMENTS};
static const rgblight_segment_t *const *original_layers;
static bool owned, awaiting_link, link_failed, finished;
static uint8_t slot, previous_phase, previous_hue, previous_value;
static uint32_t phase_at;

static void release(void) {
    if (!owned) return;
    rgblight_set_layer_state(slot, false);
    rgblight_layers = original_layers;
    owned = false;
    if (rgblight_is_enabled()) rgblight_mode_noeeprom(rgblight_get_mode());
    else rgblight_set();
}
static bool acquire(void) {
    if (owned) return true;
    original_layers = rgblight_layers;
    slot = 0;
    while (original_layers && slot < RGBLIGHT_MAX_LAYERS) {
        const rgblight_segment_t *saved_layers = pgm_read_ptr(&original_layers[slot]);
        if (!saved_layers) break;
        overlay_layers[slot++] = saved_layers;
    }
    if (slot == RGBLIGHT_MAX_LAYERS) return false;
    overlay_layers[slot] = pixels;
    overlay_layers[slot + 1] = NULL;
    rgblight_layers = overlay_layers;
    rgblight_set_layer_state(slot, true);
    previous_hue = previous_value = 255;
    owned = true;
    return true;
}
void nrf5_esb_indicator_task(void) {
    if (nrf5_underglow_test_is_active()) return;
    uint32_t now = nrf5_esb_now();
    uint8_t phase = nrf5_pair_state();
    if (phase != previous_phase) {
        previous_phase = phase; phase_at = now; finished = false;
        awaiting_link = phase == NRF5_PAIR_COMPLETE;
        link_failed = false;
    }
    uint32_t elapsed = now - phase_at;
    uint8_t hue = 0, value = 0;
    if (phase >= NRF5_PAIR_WINDOW && phase <= NRF5_PAIR_SAVING) {
        hue = phase == NRF5_PAIR_WINDOW ? 170 : 32;
        uint16_t interval = phase == NRF5_PAIR_WINDOW ? 300 : 120;
        value = (elapsed / interval) % 2 ? 0 : 96;
    } else if (phase == NRF5_PAIR_COMPLETE && !finished) {
        if (awaiting_link) {
            if (nrf5_esb_connected()) { awaiting_link = false; phase_at = now; elapsed = 0; }
            else if (elapsed < 5000) { hue = 32; value = (elapsed / 120) % 2 ? 0 : 96; }
            else { awaiting_link = false; link_failed = true; phase_at = now; elapsed = 0; }
        }
        if (!awaiting_link) {
            if (elapsed >= 1200) { finished = true; release(); return; }
            hue = link_failed ? 0 : 85; value = (elapsed / 200) % 2 ? 0 : 96;
        }
    } else if (phase >= NRF5_PAIR_FAILED && phase <= NRF5_PAIR_CANCELLED && !finished) {
        if (elapsed >= 1200) { finished = true; release(); return; }
        hue = phase == NRF5_PAIR_CANCELLED ? 32 : 0;
        value = (elapsed / 200) % 2 ? 0 : 96;
    } else { release(); return; }
    if (!acquire()) return;
    if (hue == previous_hue && value == previous_value) return;
    pixels[0].hue = previous_hue = hue;
    pixels[0].val = previous_value = value;
    // rgblight_set applies this layer after normal effects, music and Caps;
    // its power path also works when base underglow was disabled.
    rgblight_set();
}
#else
void nrf5_esb_indicator_task(void) {}
#endif
