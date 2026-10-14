#ifndef _HAL_TICK_CLOCK_H_
#define _HAL_TICK_CLOCK_H_

#include <stdint.h>

/*
 * Extends a free-running 32-bit hardware tick into a 32-bit millisecond
 * counter that wraps at 2^32 ms, so `now - then` is valid for every consumer.
 *
 * Invariant: hal_tick_clock_update() must run at least once per 2^32 ticks
 * (268 s for a 16 MHz tick); a longer gap loses whole tick wraps. Callers that
 * may be interrupted by another caller must serialise access.
 */
typedef struct {
    uint32_t base_tick;
    uint32_t base_ms;
} hal_tick_clock_t;

static inline uint32_t hal_tick_clock_update(hal_tick_clock_t *clock,
                                             uint32_t tick,
                                             uint32_t ticks_per_ms) {
    uint32_t elapsed_ms = (uint32_t)(tick - clock->base_tick) / ticks_per_ms;

    clock->base_tick += elapsed_ms * ticks_per_ms;
    clock->base_ms   += elapsed_ms;
    return clock->base_ms;
}

#endif
