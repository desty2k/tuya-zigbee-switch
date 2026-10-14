#ifndef _HAL_TICK_CLOCK_H_
#define _HAL_TICK_CLOCK_H_

#include <stdint.h>

/*
 * Extends a free-running 32-bit hardware tick into a 32-bit millisecond
 * counter that wraps at 2^32 ms, so `now - then` is valid for every consumer.
 *
 * A reading up to HAL_TICK_CLOCK_BACKSTEP_MAX ticks behind the previous one is
 * a backward step of the hardware counter (e.g. an inexact restore after a
 * sleep), not a forward wrap: the clock re-bases on it and holds its value.
 *
 * Invariant: hal_tick_clock_update() must run at least once per
 * 2^32 - HAL_TICK_CLOCK_BACKSTEP_MAX ticks (234.9 s for a 16 MHz tick); a
 * longer gap is misread as a backward step or loses whole tick wraps.
 * Callers that may be interrupted by another caller must serialise access.
 */
#define HAL_TICK_CLOCK_BACKSTEP_MAX    0x20000000u

typedef struct {
    uint32_t base_tick;
    uint32_t base_ms;
} hal_tick_clock_t;

static inline uint32_t hal_tick_clock_update(hal_tick_clock_t *clock,
                                             uint32_t tick,
                                             uint32_t ticks_per_ms) {
    uint32_t elapsed_ticks = tick - clock->base_tick;
    uint32_t elapsed_ms;

    if (elapsed_ticks != 0 &&
        (uint32_t)(clock->base_tick - tick) <= HAL_TICK_CLOCK_BACKSTEP_MAX) {
        clock->base_tick = tick;
        return clock->base_ms;
    }
    elapsed_ms        = elapsed_ticks / ticks_per_ms;
    clock->base_tick += elapsed_ms * ticks_per_ms;
    clock->base_ms   += elapsed_ms;
    return clock->base_ms;
}

#endif
