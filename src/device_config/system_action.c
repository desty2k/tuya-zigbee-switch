#include "system_action.h"

#include "device_config/reset.h"
#include "hal/system.h"

void system_action_execute(system_action_t action, uint16_t delay_ms) {
    if (action == SYSTEM_ACTION_FACTORY_RESET) {
        if (delay_ms == 0) {
            hal_factory_reset();
        } else {
            schedule_full_reset(delay_ms);
        }
    } else {
        if (delay_ms == 0) {
            hal_system_reset();
        } else {
            schedule_reboot(delay_ms);
        }
    }
}
