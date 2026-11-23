// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "wireless_power.h"
#include "nrf5_power.h"
#include "keyboard.h"
#include "matrix.h"
#include "timer.h"
#include "eeprom_nrf.h"
#include "app_util_platform.h"
#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#endif
#ifdef MUSIC_STREAM_ENABLE
#    include "music_stream.h"
#endif
#ifdef AUDIO_ENABLE
#    include "audio.h"
#endif
#ifdef NRF5_ESB_ENABLE
#    include "esb_settings.h"
#    include "esb_pair.h"
#endif
#ifdef NRF5_CONFIG_ENABLE
#    include "nrf5_lighting.h"
#endif

static bool initialized, idle;
static uint32_t previous_time, previous_activity, inactive_ticks;
static volatile bool wake_requested;

bool nrf5_wireless_idle(void) { return idle; }

void nrf5_wireless_note_activity(void) {
    wake_requested = true;
    nrf5_keyboard_scan_wake();
}

static bool keys_held(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; ++row)
        if (matrix_get_row(row)) return true;
    return false;
}

static bool busy(void) {
    if (keys_held() || nrf5_eeprom_pending()) return true;
#ifdef MUSIC_STREAM_ENABLE
    music_frame_t frame;
    if (music_stream_read(&frame)) return true;
#endif
#ifdef AUDIO_ENABLE
    if (audio_is_playing_note() || audio_is_playing_melody()) return true;
#endif
#ifdef NRF5_ESB_ENABLE
    if (nrf5_pair_busy() || nrf5_esb_settings_pending()) return true;
#endif
#ifdef NRF5_CONFIG_ENABLE
    if (nrf5_underglow_test_is_active()) return true;
#endif
    return false;
}

void nrf5_wireless_power_task(void) {
    // QMK's nRF timer wraps after 2^19 ticks; accumulate across that wrap.
    // Each timer unit is 1/1024 s, not one millisecond.
    const uint32_t now = timer_read32() & 0x7FFFFu;
    const uint32_t activity = last_input_activity_time();
    if (!initialized) {
        initialized = true;
        previous_time = now;
        previous_activity = activity;
    }
    const uint32_t delta = (now - previous_time) & 0x7FFFFu;
    previous_time = now;
    bool wake;
    CRITICAL_REGION_ENTER();
    wake = wake_requested;
    wake_requested = false;
    CRITICAL_REGION_EXIT();
    const bool active = wake || activity != previous_activity || nrf5_usb_powered() || busy();
    previous_activity = activity;
    const uint32_t timeout = (uint64_t)NRF5_WIRELESS_IDLE_TIMEOUT * 1024 / 1000;
    if (active) inactive_ticks = 0;
    else if (inactive_ticks < timeout) inactive_ticks += delta > timeout - inactive_ticks ? timeout - inactive_ticks : delta;
    const bool next_idle = !active && timeout && inactive_ticks >= timeout;
    if (next_idle == idle) return;
    idle = next_idle;
#ifdef RGBLIGHT_ENABLE
    // A rendering gate, not an enabled/HSV/EEPROM change. Restore static
    // colours as well as animations on wake; an OFF base keeps its Caps layer.
    if (!idle && rgblight_is_enabled()) rgblight_mode_noeeprom(rgblight_get_mode());
    else rgblight_set();
#endif
}

#ifdef NRF5_MATRIX_IDLE_ENABLE
#    include "nrfx_gpiote.h"
#    include "nrf_gpio.h"

#    if defined(DIRECT_PINS) || defined(SPLIT_KEYBOARD) || !defined(MATRIX_ROW_PINS) || !defined(MATRIX_COL_PINS)
#        error NRF5_MATRIX_IDLE_ENABLE requires a non-split diode matrix
#    endif
#    if DIODE_DIRECTION == COL2ROW
static const uint8_t outputs[] = MATRIX_ROW_PINS;
static const uint8_t inputs[] = MATRIX_COL_PINS;
#    elif DIODE_DIRECTION == ROW2COL
static const uint8_t outputs[] = MATRIX_COL_PINS;
static const uint8_t inputs[] = MATRIX_ROW_PINS;
#    else
#        error Unsupported diode direction
#    endif

// Generic QMK diode-matrix pin restore hook (not part of matrix.h).
extern void matrix_init_pins(void);
static bool gpio_attempted, gpio_ready, parked;

static void disable_sense(void) {
    for (unsigned i = 0; i < sizeof(inputs); ++i) nrfx_gpiote_in_event_disable(inputs[i]);
}

static void key_wake(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t polarity) {
    (void)pin; (void)polarity;
    // Do not scan, render, touch Flash, or restart timers in interrupt context.
    // Stop level sensing now so a held key cannot cause an interrupt storm.
    disable_sense();
    nrf5_wireless_note_activity();
}

static bool init_gpio(void) {
    if (gpio_attempted) return gpio_ready;
    gpio_attempted = true;
    // Unsupported/no-pin entries must fail back to polling, not reach an SDK assert.
    for (unsigned i = 0; i < sizeof(inputs); ++i) if (inputs[i] >= 48) return false;
    for (unsigned i = 0; i < sizeof(outputs); ++i) if (outputs[i] >= 48) return false;
    if (!nrfx_gpiote_is_init() && nrfx_gpiote_init() != NRFX_SUCCESS) return false;
    nrfx_gpiote_in_config_t config = NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(false);
    config.pull = NRF_GPIO_PIN_PULLUP;
    for (unsigned i = 0; i < sizeof(inputs); ++i) {
        if (nrfx_gpiote_in_init(inputs[i], &config, key_wake) != NRFX_SUCCESS) {
            while (i) nrfx_gpiote_in_uninit(inputs[--i]);
            matrix_init_pins();
            // Resource contention is recoverable: retain ordinary fast polling.
            return false;
        }
    }
    gpio_ready = true;
    return true;
}

void nrf5_wireless_prepare_scan(void) {
    if (!parked) return;
    disable_sense();
    // Restore QMK's normal unselected pin state before selecting rows/columns.
    matrix_init_pins();
    parked = false;
}

void nrf5_wireless_finish_scan(void) {
    if (!idle || wake_requested || keys_held() || !init_gpio()) return;
    // All drive lines low lets any switch pull a sense input low. Idle has no
    // held keys, so there is no pull-up DC path through a pressed switch.
    for (unsigned i = 0; i < sizeof(outputs); ++i) {
        nrf_gpio_pin_clear(outputs[i]);
        nrf_gpio_cfg_output(outputs[i]);
    }
    parked = true;
    for (unsigned i = 0; i < sizeof(inputs); ++i) nrfx_gpiote_in_event_enable(inputs[i], true);
    // Close the arm/read race even if the pin was already low before enable.
    for (unsigned i = 0; i < sizeof(inputs); ++i) {
        if (!nrf_gpio_pin_read(inputs[i])) { key_wake(inputs[i], NRF_GPIOTE_POLARITY_HITOLO); break; }
    }
}

void nrf5_wireless_before_system_off(void) {
    // Hand sense-pin ownership back to the System OFF wake configuration.
    // A stale GPIOTE callback must never clear those new wake senses.
    nrf5_wireless_prepare_scan();
    if (gpio_ready) {
        for (unsigned i = 0; i < sizeof(inputs); ++i) nrfx_gpiote_in_uninit(inputs[i]);
        gpio_ready = false;
    }
}

uint32_t nrf5_wireless_scan_interval(void) {
    return parked && idle && !wake_requested ? NRF5_WIRELESS_IDLE_SCAN_INTERVAL : KEYBOARD_SCAN_INTERVAL;
}
#else
void nrf5_wireless_prepare_scan(void) {}
void nrf5_wireless_finish_scan(void) {}
void nrf5_wireless_before_system_off(void) {}
uint32_t nrf5_wireless_scan_interval(void) { return KEYBOARD_SCAN_INTERVAL; }
#endif
