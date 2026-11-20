#ifndef WAIT_API_H
#define WAIT_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nrf_delay.h"
#ifdef AUDIO_ENABLE
// Continue polling melodies during QMK's mute/shutdown waits, in thread mode.
void nrf_audio_wait_ms(uint32_t ms);
#    define wait_ms(ms) nrf_audio_wait_ms(ms)
#else
#    define wait_ms(ms) nrf_delay_ms(ms)
#endif
#define wait_us(us) nrf_delay_us(us)

#ifdef __cplusplus
}
#endif

#endif
