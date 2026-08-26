#ifndef _SYSTEM_ACTION_H_
#define _SYSTEM_ACTION_H_

#include <stdint.h>

typedef enum {
    SYSTEM_ACTION_FACTORY_RESET,
    SYSTEM_ACTION_REBOOT,
} system_action_t;

void system_action_execute(system_action_t action, uint16_t delay_ms);

#endif
