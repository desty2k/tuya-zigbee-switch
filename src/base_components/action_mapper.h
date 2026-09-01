#ifndef _ACTION_MAPPER_H_
#define _ACTION_MAPPER_H_

#include "base_components/gesture_fsm.h"
#include "base_components/relay_controller.h"
#include <stdbool.h>
#include <stdint.h>

#define ACTION_MAPPER_MAX_RECORDS    32

typedef enum {
    ACTION_TRIGGER_DOWN,
    ACTION_TRIGGER_UP,
    ACTION_TRIGGER_HOLD,
    ACTION_TRIGGER_N_CLICK,
} action_trigger_type_t;

typedef enum {
    ACTION_NONE = 0,
    ACTION_RELAY,
    ACTION_COVER,
    ACTION_BIND_ONOFF,
    ACTION_BIND_LEVEL,
    ACTION_BUTTON_EVENT,
    ACTION_SYSTEM,
} action_type_t;

typedef enum {
    ACTION_SYSTEM_FACTORY_RESET,
    ACTION_SYSTEM_REBOOT,
} action_system_t;

typedef struct {
    uint8_t               button_id;
    action_trigger_type_t type;
    uint8_t               count;
} action_trigger_t;

typedef struct {
    action_type_t type;
    uint8_t       target_id;
    uint16_t      argument;
} action_target_t;

typedef struct {
    action_trigger_t trigger;
    action_target_t  target;
} action_mapping_t;

typedef void (*action_cover_sink_t)(void *context, uint8_t cover_id,
                                    uint16_t command);
typedef void (*action_binding_sink_t)(void *context, uint8_t target_id,
                                      action_type_t type, uint16_t intent);
typedef void (*action_button_event_sink_t)(void *context, uint8_t target_id,
                                           uint16_t event);
typedef void (*action_system_sink_t)(void *context, uint8_t action,
                                     uint16_t delay_ms);

typedef struct {
    action_cover_sink_t        cover;
    action_binding_sink_t      binding;
    action_button_event_sink_t button_event;
    action_system_sink_t       system;
    void *                     context;
} action_mapper_sinks_t;

void action_mapper_init(void);
bool action_mapper_add(const action_mapping_t *mapping);
void action_mapper_clear(void);
void action_mapper_set_sinks(const action_mapper_sinks_t *sinks);
void action_mapper_dispatch(const action_trigger_t *trigger);
relay_result_t action_mapper_relay_request(uint8_t relay_id,
                                           relay_request_type_t type,
                                           relay_request_source_t source);
void action_mapper_cover_action(uint8_t cover_id, uint16_t command);
void action_mapper_binding_intent(uint8_t target_id, action_type_t type,
                                  uint16_t intent);
void action_mapper_add_reset_button(uint8_t button_id, bool hold_reset);

#endif
