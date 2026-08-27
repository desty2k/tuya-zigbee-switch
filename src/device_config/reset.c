#include "reset.h"
#include "base_components/timer_service.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "hal/system.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RESET_ACTION_FULL_RESET,
    RESET_ACTION_REBOOT,
} reset_action_t;

static app_timer_t    reset_timer;
static reset_action_t reset_action;
static bool           reset_timer_initialized;

__attribute__((noreturn)) void reset_all() {
    printf("RESET ALL!\r\n");
    hal_nvm_clear_all();
    hal_factory_reset();
    hal_system_reset();
}

static void reset_timer_handler(void *arg) {
    (void)arg;
    if (reset_action == RESET_ACTION_FULL_RESET) {
        reset_all();
    }
    hal_system_reset();
}

static void reset_schedule(reset_action_t action, uint16_t delay_ms) {
    if (!reset_timer_initialized) {
        timer_init(&reset_timer, reset_timer_handler, NULL);
        reset_timer_initialized = true;
    }
    reset_action = action;
    timer_restart(&reset_timer,
                  delay_ms != 0 ? delay_ms : DEFAULT_RESET_DELAY_MS);
}

void schedule_full_reset(uint16_t delay_ms) {
    reset_schedule(RESET_ACTION_FULL_RESET, delay_ms);
}

void schedule_reboot(uint16_t delay_ms) {
    reset_schedule(RESET_ACTION_REBOOT, delay_ms);
}
