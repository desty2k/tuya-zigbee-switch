#include "action_mapper.h"

#include "device_config/device_params_nv.h"
#include <stddef.h>

static action_mapping_t      action_mappings[ACTION_MAPPER_MAX_RECORDS];
static uint8_t               action_mapping_count;
static action_mapper_sinks_t action_sinks;

static bool action_trigger_matches(const action_trigger_t *expected,
                                   const action_trigger_t *actual) {
    return expected->button_id == actual->button_id &&
           expected->type == actual->type &&
           (expected->count == 0 || expected->count == actual->count);
}

static void action_mapper_execute(const action_target_t *target) {
    switch (target->type) {
    case ACTION_RELAY:
        (void)action_mapper_relay_request(target->target_id,
                                          (relay_request_type_t)target->argument,
                                          RELAY_SOURCE_GESTURE);
        break;
    case ACTION_COVER:
        action_mapper_cover_action(target->target_id, target->argument);
        break;
    case ACTION_BIND_ONOFF:
    case ACTION_BIND_LEVEL:
        action_mapper_binding_intent(target->target_id, target->type,
                                     target->argument);
        break;
    case ACTION_BUTTON_EVENT:
        if (action_sinks.button_event != NULL) {
            action_sinks.button_event(action_sinks.context, target->target_id,
                                      target->argument);
        }
        break;
    case ACTION_SYSTEM:
        if (action_sinks.system != NULL) {
            action_sinks.system(action_sinks.context, target->target_id,
                                target->argument);
        }
        break;
    case ACTION_NONE:
    default:
        break;
    }
}

static void action_mapper_gesture(const gesture_event_t *event, void *context) {
    action_trigger_t trigger;

    (void)context;
    if (event->type == GESTURE_HOLD_START) {
        trigger.type = ACTION_TRIGGER_HOLD;
    } else if (event->type == GESTURE_N_CLICK) {
        if (g_multi_press_reset_count != 0 &&
            event->count >= g_multi_press_reset_count) {
            trigger.type      = ACTION_TRIGGER_N_CLICK;
            trigger.count     = 0;
            trigger.button_id = event->button_id;
            action_mapper_dispatch(&trigger);
        }
        return;
    } else {
        return;
    }
    trigger.button_id = event->button_id;
    trigger.count     = 0;
    action_mapper_dispatch(&trigger);
}

void action_mapper_init(void) {
    action_mapper_clear();
    action_sinks = (action_mapper_sinks_t){ 0 };
    gesture_fsm_register_sink(action_mapper_gesture, NULL);
}

bool action_mapper_add(const action_mapping_t *mapping) {
    if (mapping == NULL || mapping->trigger.button_id >= BUTTON_INPUT_MAX ||
        action_mapping_count >= ACTION_MAPPER_MAX_RECORDS) {
        return false;
    }
    action_mappings[action_mapping_count++] = *mapping;
    return true;
}

void action_mapper_clear(void) {
    action_mapping_count = 0;
}

void action_mapper_set_sinks(const action_mapper_sinks_t *sinks) {
    action_sinks = sinks == NULL ? (action_mapper_sinks_t){ 0 } : *sinks;
}

void action_mapper_dispatch(const action_trigger_t *trigger) {
    if (trigger == NULL) {
        return;
    }
    for (uint8_t i = 0; i < action_mapping_count; i++) {
        if (action_trigger_matches(&action_mappings[i].trigger, trigger)) {
            action_mapper_execute(&action_mappings[i].target);
        }
    }
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

void action_mapper_binding_intent(uint8_t target_id, action_type_t type,
                                  uint16_t intent) {
    if (action_sinks.binding != NULL) {
        action_sinks.binding(action_sinks.context, target_id, type, intent);
    }
}

void action_mapper_add_reset_button(uint8_t button_id, bool hold_reset) {
    (void)action_mapper_add(&(action_mapping_t){
        .trigger = { .button_id = button_id,
                     .type      = hold_reset ? ACTION_TRIGGER_HOLD : ACTION_TRIGGER_N_CLICK,
                     .count     = 0 },
        .target = { .type      = ACTION_SYSTEM,
                    .target_id = ACTION_SYSTEM_FACTORY_RESET,
                    .argument  = 0 },
    });
}
