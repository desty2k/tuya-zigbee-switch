#include "action_mapper.h"

#include "device_config/device_params_nv.h"
#include "device_config/system_action.h"
#include <stddef.h>

typedef struct {
    bool reset_clicks;
    bool reset_hold;
} action_button_mapping_t;

static action_button_mapping_t action_button_mappings[BUTTON_INPUT_MAX];

static void action_mapper_gesture(const gesture_event_t *event, void *arg) {
    action_button_mapping_t *mapping;

    (void)arg;
    if (event->button_id >= BUTTON_INPUT_MAX) {
        return;
    }
    mapping = &action_button_mappings[event->button_id];
    if (event->type == GESTURE_HOLD_START && mapping->reset_hold) {
        system_action_execute(SYSTEM_ACTION_FACTORY_RESET, 0);
    } else if (event->type == GESTURE_N_CLICK && mapping->reset_clicks &&
               g_multi_press_reset_count != 0 &&
               event->count >= g_multi_press_reset_count) {
        system_action_execute(SYSTEM_ACTION_FACTORY_RESET, 0);
    }
}

void action_mapper_init(void) {
    for (uint8_t i = 0; i < BUTTON_INPUT_MAX; i++) {
        action_button_mappings[i].reset_clicks = false;
        action_button_mappings[i].reset_hold   = false;
    }
    gesture_fsm_register_sink(action_mapper_gesture, NULL);
}

void action_mapper_add_reset_button(uint8_t button_id, bool hold_reset) {
    if (button_id >= BUTTON_INPUT_MAX) {
        return;
    }
    action_button_mappings[button_id].reset_clicks = !hold_reset;
    action_button_mappings[button_id].reset_hold   = hold_reset;
}
