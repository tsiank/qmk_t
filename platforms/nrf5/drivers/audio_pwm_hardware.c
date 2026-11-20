// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio.h"
#include "nrfx_pwm.h"
#include "nrf_gpio.h"
#include "timer.h"
#include "nrf_delay.h"
#include <math.h>

// PWM0 belongs to WS2812. PWM1 drives the two terminals of the piezo.
static const nrfx_pwm_t speaker = NRFX_PWM_INSTANCE(1);
static bool initialized, running;
static uint16_t last_update, last_period;
static nrf_pwm_values_individual_t sample;
static const nrf_pwm_sequence_t sequence = {
    .values.p_individual = &sample, .length = NRF_PWM_VALUES_LENGTH(sample),
};

void audio_driver_initialize_impl(void) {
    nrf_gpio_cfg_output(AUDIO_PIN);
    nrf_gpio_cfg_output(AUDIO_PIN_ALT);
    nrf_gpio_pin_clear(AUDIO_PIN);
    nrf_gpio_pin_clear(AUDIO_PIN_ALT);
}

static void silence(void) {
    if (initialized) {
        nrfx_pwm_stop(&speaker, true);
        nrfx_pwm_uninit(&speaker);
        initialized = false;
    }
    nrf_gpio_pin_clear(AUDIO_PIN);
    nrf_gpio_pin_clear(AUDIO_PIN_ALT);
    last_period = 0;
}

void audio_driver_start_impl(void) {
    running = true;
    last_update = timer_read() - 4;
}

void audio_driver_stop_impl(void) {
    running = false;
    silence();
}

// Called in thread mode. No app_timer IRQ executes QMK or touches its note stack.
void nrf_audio_task(void) {
    if (!running || timer_elapsed(last_update) < 4) return;
    last_update = timer_read();
    audio_update_state();
    float frequency = audio_get_processed_frequency(0);
    if (!isfinite(frequency) || frequency < 31.0f || frequency > 20000.0f) {
        silence();
        return;
    }
    uint16_t period = (uint16_t)(1000000.0f / frequency + 0.5f);
    if (initialized && period == last_period) return;
    silence();
    nrfx_pwm_config_t config = NRFX_PWM_DEFAULT_CONFIG;
    config.output_pins[0] = AUDIO_PIN;
    config.output_pins[1] = AUDIO_PIN_ALT;
    config.output_pins[2] = NRFX_PWM_PIN_NOT_USED;
    config.output_pins[3] = NRFX_PWM_PIN_NOT_USED;
    config.base_clock = NRF_PWM_CLK_1MHz;
    config.count_mode = NRF_PWM_MODE_UP;
    config.top_value = period;
    config.load_mode = NRF_PWM_LOAD_INDIVIDUAL;
    config.step_mode = NRF_PWM_STEP_AUTO;
    // Opposite polarity, identical duty: balanced AC across the piezo.
    sample.channel_0 = period / 2;
    sample.channel_1 = 0x8000 | (period / 2);
    sample.channel_2 = sample.channel_3 = 0;
    if (nrfx_pwm_init(&speaker, &config, NULL) != NRFX_SUCCESS) return;
    initialized = true;
    last_period = period;
    nrfx_pwm_simple_playback(&speaker, &sequence, 1, NRFX_PWM_FLAG_LOOP);
}

void nrf_audio_wait_ms(uint32_t ms) {
    while (ms--) {
        nrf_delay_ms(1);
        if (__get_IPSR() == 0) nrf_audio_task();
    }
}
