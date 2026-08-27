#ifndef _COVER_SWITCH_CLUSTER_H_
#define _COVER_SWITCH_CLUSTER_H_

#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "hal/zigbee.h"
#include "zigbee/button_event_cluster.h"
#include <stdint.h>

typedef struct {
    uint8_t  cover_index;
    uint8_t  reversal;
    uint8_t  local_mode;
    uint8_t  binded_mode;
    uint8_t  switch_type;
    uint16_t multi_click_gap_ms;
    uint16_t debounce_ms;
} zigbee_cover_switch_cluster_config;

typedef struct {
    // Parameters
    uint8_t                     cover_switch_idx;
    uint8_t                     endpoint;
    uint8_t                     open_button_id;
    uint8_t                     close_button_id;
    uint16_t                    hold_duration_ms;

    // Attributes
    uint8_t                     switch_type;
    uint8_t                     cover_index;
    uint8_t                     reversal;
    uint8_t                     local_mode;
    uint8_t                     binded_mode;
    uint16_t                    multi_click_gap_ms;
    uint16_t                    debounce_ms;
    hal_zigbee_attribute        config_attr_infos[6];

    uint16_t                    present_value;
    hal_zigbee_attribute        multistate_attr_infos[4];

    // State
    uint8_t                     binded_moving;
    zigbee_button_event_cluster button_event;
} zigbee_cover_switch_cluster;

void cover_switch_cluster_add_to_endpoint(zigbee_cover_switch_cluster *cluster,
                                          hal_zigbee_endpoint *endpoint);

void cover_switch_cluster_callback_attr_write_trampoline(uint8_t endpoint, uint16_t attribute_id);
void cover_switch_cluster_register_input(void);
void cover_switch_cluster_register_gestures(void);

#endif
