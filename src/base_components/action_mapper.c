#include "action_mapper.h"

#include "hal/system.h"
#include <stddef.h>

typedef enum {
    RESET_TRIGGER_NONE = 0,
    RESET_TRIGGER_HOLD,
    RESET_TRIGGER_MULTI_PRESS,
} reset_trigger_t;

static uint8_t reset_triggers[BUTTON_INPUT_MAX];
/* Kept across action_mapper_init(): set from NVM before the graph is wired. */
static uint8_t multi_press_reset_count = 10;
static action_mapper_sinks_t action_sinks;

static bool action_mapper_is_reset(const gesture_event_t *event) {
    uint8_t trigger = reset_triggers[event->button_id];

    if (event->type == GESTURE_HOLD_START) {
        return trigger == RESET_TRIGGER_HOLD;
    }
    return event->type == GESTURE_N_CLICK &&
           trigger == RESET_TRIGGER_MULTI_PRESS &&
           multi_press_reset_count != 0 &&
           event->count >= multi_press_reset_count;
}

static void action_mapper_gesture(const gesture_event_t *event, void *context) {
    (void)context;
    if (event->button_id < BUTTON_INPUT_MAX &&
        action_mapper_is_reset(event)) {
        if (action_sinks.factory_reset != NULL) {
            action_sinks.factory_reset();
        } else {
            hal_factory_reset();
        }
    }
}

void action_mapper_init(void) {
    for (uint8_t i = 0; i < BUTTON_INPUT_MAX; i++) {
        reset_triggers[i] = RESET_TRIGGER_NONE;
    }
    action_sinks = (action_mapper_sinks_t){ 0 };
    gesture_fsm_register_sink(action_mapper_gesture, NULL);
}

void action_mapper_set_sinks(const action_mapper_sinks_t *sinks) {
    action_sinks = sinks == NULL ? (action_mapper_sinks_t){ 0 } : *sinks;
}

void action_mapper_set_multi_press_reset_count(uint8_t count) {
    multi_press_reset_count = count;
}

bool action_mapper_add_reset_button(uint8_t button_id, bool hold_reset) {
    if (button_id >= BUTTON_INPUT_MAX) {
        return false;
    }
    reset_triggers[button_id] =
        hold_reset ? RESET_TRIGGER_HOLD : RESET_TRIGGER_MULTI_PRESS;
    return true;
}

relay_result_t action_mapper_relay_request(uint8_t relay_id,
                                           relay_request_type_t type,
                                           relay_request_source_t source) {
    return relay_ctrl_submit(&(relay_request_t){
        .relay_id = relay_id,
        .type     = type,
        .source   = source,
    });
}

void action_mapper_cover_action(uint8_t cover_id, uint16_t command) {
    if (action_sinks.cover != NULL) {
        action_sinks.cover(action_sinks.context, cover_id, command);
    }
}
