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
 * least once per tick wrap: from the main loop, and from a periodic event timer
 * that also bounds deep-retention sleep on end devices.
 */
#define MILLIS_REFRESH_PERIOD_MS    60000

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
    (void)hal_millis();
    return 0;
}

void telink_millis_poll(void) {
    (void)hal_millis();
    if (millis_refresh_timer == NULL) {
        millis_refresh_timer = ev_timer_taskPost(millis_refresh, NULL,
                                                 MILLIS_REFRESH_PERIOD_MS);
    }
}
