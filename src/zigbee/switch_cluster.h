#ifndef _SWITCH_CLUSTER_H_
#define _SWITCH_CLUSTER_H_

#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "base_components/indicator_feedback.h"
#include "hal/zigbee.h"
#include "zigbee/button_event_cluster.h"
#include <stdint.h>

typedef struct {
    uint8_t  mode;
    uint8_t  action;
    uint8_t  relay_mode;
    uint8_t  relay_index;
    uint16_t hold_duration_ms;
    uint8_t  level_move_rate;
    uint8_t  binded_mode;
    uint16_t multi_click_gap_ms;
    uint16_t debounce_ms;
} zigbee_switch_cluster_config;

typedef struct {
    uint8_t                     switch_idx;
    uint8_t                     endpoint;
    uint8_t                     mode;
    uint8_t                     action;
    uint8_t                     relay_mode;
    uint8_t                     relay_index;
    uint8_t                     binded_mode;
    uint8_t                     button_id;
    uint16_t                    hold_duration_ms;
    uint16_t                    multi_click_gap_ms;
    uint16_t                    debounce_ms;
    hal_zigbee_attribute        attr_infos[8];
    uint16_t                    multistate_state;
    hal_zigbee_attribute        multistate_attr_infos[4];
    uint8_t                     level_move_rate;
    uint8_t                     level_move_direction;
    indicator_feedback_t *      indicator_led;
    zigbee_button_event_cluster button_event;
} zigbee_switch_cluster;

void switch_cluster_add_to_endpoint(zigbee_switch_cluster *cluster,
                                    hal_zigbee_endpoint *endpoint);

void switch_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                   uint16_t attribute_id);

void update_switch_clusters(void);
void switch_cluster_register_input(void);
void switch_cluster_register_gestures(void);

#endif
