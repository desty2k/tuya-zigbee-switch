#ifndef _ACTION_MAPPER_H_
#define _ACTION_MAPPER_H_

#include "base_components/gesture_fsm.h"
#include "base_components/relay_controller.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*action_cover_sink_t)(void *context, uint8_t cover_id,
                                    uint16_t command);

typedef struct {
    action_cover_sink_t cover;
    void *              context;
    /* Factory reset gesture; NULL: hal_factory_reset(). */
    void (*factory_reset)(void);
} action_mapper_sinks_t;

/** Forget every reset button and sink; registers the gesture sink. */
void action_mapper_init(void);
void action_mapper_set_sinks(const action_mapper_sinks_t *sinks);

/**
 * Make a button factory-reset the device: on hold when hold_reset, otherwise
 * on a multi-click of at least the multi-press reset count.
 */
bool action_mapper_add_reset_button(uint8_t button_id, bool hold_reset);

/** Clicks that factory-reset a non-onboard button; 0 disables, default 10. */
void action_mapper_set_multi_press_reset_count(uint8_t count);

relay_result_t action_mapper_relay_request(uint8_t relay_id,
                                           relay_request_type_t type,
                                           relay_request_source_t source);
void action_mapper_cover_action(uint8_t cover_id, uint16_t command);

#endif
