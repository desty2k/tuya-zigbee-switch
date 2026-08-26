#ifndef _ACTION_MAPPER_H_
#define _ACTION_MAPPER_H_

#include "base_components/gesture_fsm.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACTION_NONE = 0,
    ACTION_RELAY,
    ACTION_BIND_ONOFF,
    ACTION_BIND_LEVEL,
    ACTION_ZB_BUTTON_EVENT,
    ACTION_SYSTEM,
} action_type_t;

void action_mapper_init(void);
void action_mapper_add_reset_button(uint8_t button_id, bool hold_reset);

#endif
