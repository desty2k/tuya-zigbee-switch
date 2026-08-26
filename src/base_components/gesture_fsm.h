#ifndef _GESTURE_FSM_H_
#define _GESTURE_FSM_H_

#include "base_components/button_input.h"
#include "base_components/timer_service.h"
#include <stdbool.h>
#include <stdint.h>

#define GESTURE_MAX_N_CLICK    10
#define GESTURE_SINK_MAX       4

typedef enum {
    GESTURE_HOLD_START,
    GESTURE_HOLD_END,
    GESTURE_N_CLICK,
} gesture_type_t;

typedef struct {
    uint8_t        button_id;
    gesture_type_t type;
    uint8_t        count;
    uint16_t       duration_ms;
    uint32_t       press_id;
    uint32_t       timestamp_ms;
} gesture_event_t;

typedef struct {
    uint16_t hold_ms;
    uint16_t multi_click_gap_ms;
} gesture_config_t;

typedef void (*gesture_sink_t)(const gesture_event_t *event, void *arg);

void gesture_fsm_init(void);
void gesture_fsm_add(uint8_t button_id, const gesture_config_t *config);
void gesture_fsm_set_hold_ms(uint8_t button_id, uint16_t hold_ms);
void gesture_fsm_set_multi_click_gap_ms(uint8_t button_id, uint16_t gap_ms);
bool gesture_fsm_hold_active(uint8_t button_id);
void gesture_fsm_register_sink(gesture_sink_t sink, void *arg);

#endif
