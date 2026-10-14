#include "hal/timer.h"
#include "sl_sleeptimer.h"

/*
 * The 32-bit sleeptimer tick wraps every 36.4 h at 32768 Hz, which would make
 * the converted millisecond value wrap short of 2^32. Converting the 64-bit
 * tick count keeps hal_millis() a true 32-bit millisecond counter.
 */
uint32_t hal_millis() {
    uint64_t ms = 0;

    (void)sl_sleeptimer_tick64_to_ms(sl_sleeptimer_get_tick_count64(), &ms);
    return (uint32_t)ms;
}
