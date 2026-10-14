#include "telink_size_t_hack.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

#include "hal/tick_clock.h"
#include "hal/timer.h"
#include "telink_zigbee_hal.h"
#include <stdint.h>

/*
 * clock_time() is a 32-bit 16 MHz tick that wraps every 268 s. hal_millis()
 * extends it to a full 32-bit millisecond count, which needs an update at
 * least every 234.9 s (see hal/tick_clock.h).
 *
 * Awake, the main loop updates it on every pass. Asleep:
 *  - suspend (routers, mains end devices) lasts at most 1 s;
 *  - deep sleep without retention reboots, restarting the count;
 *  - deep retention lasts until the nearest event timer. Before it, a one-shot
 *    refresh timer is armed only when that nearest timer is further away than
 *    MILLIS_REFRESH_BOUND_MS, so routine wakes (e.g. long polls) are not
 *    multiplied and a device with no pending timer still reaches deep sleep.
 */
#define MILLIS_REFRESH_BOUND_MS    200000

static hal_tick_clock_t  millis_clock;
static ev_timer_event_t *millis_refresh_timer;

uint32_t hal_millis() {
    // Interrupt handlers call hal_millis() too; the update must be atomic.
    u32      irq_state = drv_disable_irq();
    uint32_t now_ms    = hal_tick_clock_update(&millis_clock, clock_time(),
                                               CLOCK_16M_SYS_TIMER_CLK_1MS);

    drv_restore_irq(irq_state);
    return now_ms;
}

static s32 millis_refresh(void *arg) {
    (void)arg;
    millis_refresh_timer = NULL;
    (void)hal_millis();
    return -1;
}

void telink_millis_poll(void) {
    (void)hal_millis();
}

void telink_millis_prepare_sleep(void) {
    ev_timer_event_t *nearest;

    if (millis_refresh_timer != NULL) {
        ev_timer_taskCancel(&millis_refresh_timer);
    }
    nearest = ev_timer_nearestGet();
    if (nearest != NULL && nearest->timeout > MILLIS_REFRESH_BOUND_MS) {
        millis_refresh_timer = ev_timer_taskPost(millis_refresh, NULL,
                                                 MILLIS_REFRESH_BOUND_MS);
    }
}
