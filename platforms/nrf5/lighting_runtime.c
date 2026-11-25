// SPDX-License-Identifier: GPL-2.0-or-later
#include "lighting_runtime.h"
#ifdef NRF5_WIRELESS_POWER_ENABLE
#    include "wireless_power.h"
#endif

#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix.h"
bool rgb_matrix_platform_suspended(void) {
#    ifdef NRF5_WIRELESS_POWER_ENABLE
    return nrf5_wireless_idle();
#    else
    return false;
#    endif
}
#endif

#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#    include "nrf_gpio.h"
#    include "wait.h"
#    include "sync_timer.h"

static bool idle_blanked;
#    if defined(RGBLIGHT_LAYERS) && defined(RGBLIGHT_LAYERS_OVERRIDE_RGB_OFF)
static uint8_t refresh_remaining;
static uint16_t refresh_at;
#    endif

void rgb_pwr_on(void) {
#    ifdef NRF5_WIRELESS_POWER_ENABLE
    if (nrf5_wireless_idle()) return;
#    endif
    bool was_off = false;
#    ifdef RGB_PWR_PIN
    was_off |= nrf_gpio_pin_out_read(RGB_PWR_PIN) != 0;
    nrf_gpio_pin_write(RGB_PWR_PIN, 0);
#    endif
#    ifdef RGB_PWR_PIN_REVERSE
    was_off |= nrf_gpio_pin_out_read(RGB_PWR_PIN_REVERSE) == 0;
    nrf_gpio_pin_write(RGB_PWR_PIN_REVERSE, 1);
#    endif
    if (was_off) wait_us(2000);
}

void rgb_pwr_off(void) {
#    ifdef RGB_PWR_PIN
    nrf_gpio_pin_write(RGB_PWR_PIN, 1);
#    endif
#    ifdef RGB_PWR_PIN_REVERSE
    nrf_gpio_pin_write(RGB_PWR_PIN_REVERSE, 0);
#    endif
}

void rgblight_platform_init(void) {
#    ifdef RGB_PWR_PIN
    nrf_gpio_cfg_output(RGB_PWR_PIN);
#    endif
#    ifdef RGB_PWR_PIN_REVERSE
    nrf_gpio_cfg_output(RGB_PWR_PIN_REVERSE);
#    endif
}

bool rgblight_platform_prepare(bool power_needed) {
#    ifdef NRF5_WIRELESS_POWER_ENABLE
    if (nrf5_wireless_idle()) {
        if (!idle_blanked) {
            rgblight_driver.set_color_all(0, 0, 0);
            rgblight_driver.flush();
            rgb_pwr_off();
            idle_blanked = true;
        }
        return false;
    }
#    endif
    idle_blanked = false;
    if (power_needed) rgb_pwr_on();
    return true;
}

void rgblight_platform_finish(bool power_needed) {
    if (!power_needed) rgb_pwr_off();
}

void rgblight_platform_task(bool layer_changed, bool animation_running) {
#    if defined(RGBLIGHT_LAYERS) && defined(RGBLIGHT_LAYERS_OVERRIDE_RGB_OFF)
    if (layer_changed) {
        refresh_remaining = 2;
        refresh_at = sync_timer_read();
    }
    if (refresh_remaining && (uint16_t)(sync_timer_read() - refresh_at) >= 20) {
        --refresh_remaining;
        refresh_at = sync_timer_read();
        // Compose current state, never replay a cached Caps/colour snapshot.
        if (!rgblight_is_enabled() || !animation_running) rgblight_set();
    }
#    endif
}
#endif
