#include "reset.h"
#include "base_components/timer_service.h"
#include "hal/system.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static app_timer_t reset_timer;
static bool        reset_timer_initialized;

static void reset_timer_handler(void *arg) {
    (void)arg;
    hal_system_reset();
}

void schedule_reboot(uint16_t delay_ms) {
    if (!reset_timer_initialized) {
        timer_init(&reset_timer, reset_timer_handler, NULL);
        reset_timer_initialized = true;
    }
    timer_restart(&reset_timer,
                  delay_ms != 0 ? delay_ms : DEFAULT_RESET_DELAY_MS);
}
