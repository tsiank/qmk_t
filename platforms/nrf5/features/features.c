// SPDX-License-Identifier: GPL-2.0-or-later
#include "nrf5_lighting.h"
#include "nrf5_features.h"
#ifdef KEYBOARD_CONTROL_ENABLE
void nrf5_keyboard_control_task(void);
#endif
#ifdef NRF5_CLOCK_ENABLE
#    include "nrf5_clock.h"
#endif
void nrf5_features_task(void) {
#ifdef NRF5_CLOCK_ENABLE
    nrf5_clock_task();
#endif
#ifdef MUSIC_STREAM_ENABLE
    nrf5_music_task();
#endif
#ifdef KEYBOARD_CONTROL_ENABLE
    nrf5_keyboard_control_task();
#endif
    nrf5_underglow_test_task();
    nrf5_lighting_task();
}
