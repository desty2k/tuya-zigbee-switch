#include "feature_wiring.h"

#include "base_components/action_mapper.h"
#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "base_components/interlock.h"
#include "base_components/relay_controller.h"
#include "base_components/relay_driver.h"
#include "zigbee/cover_cluster.h"
#include "zigbee/cover_switch_cluster.h"
#include "zigbee/consts.h"
#include "zigbee/button_event_cluster.h"
#include "zigbee/switch_cluster.h"

extern button_config_t             button_configs[11];
extern gesture_config_t            gesture_configs[11];
extern input_button_role_t         button_roles[11];
extern uint8_t                     buttons_cnt;
extern zigbee_switch_cluster       switch_clusters[];
extern uint8_t                     switch_clusters_cnt;
extern zigbee_cover_switch_cluster cover_switch_clusters[];
extern uint8_t                     cover_switch_clusters_cnt;
extern relay_driver_t              relay_drivers[10];
extern relay_config_t              relay_configs[10];
extern uint8_t                     relays_cnt;
extern zigbee_cover_cluster        cover_clusters[];
extern uint8_t                     cover_clusters_cnt;

extern device_interlock_config_t interlock_configs[INTERLOCK_MAX_GROUPS];
extern uint8_t interlock_configs_cnt;

void feature_wiring_init_relays(void) {
    relay_ctrl_init();
    for (uint8_t i = 0; i < relays_cnt; i++) {
        relay_ctrl_add(&relay_drivers[i], &relay_configs[i]);
    }
    for (uint8_t group_index = 0; group_index < interlock_configs_cnt;
         group_index++) {
        uint8_t group_id = group_index + 1;

        interlock_add_group(group_id,
                            interlock_configs[group_index].relay_mask,
                            interlock_configs[group_index].dead_time_ms);
        for (uint8_t relay_id = 0; relay_id < relays_cnt; relay_id++) {
            if ((interlock_configs[group_index].relay_mask &
                 ((uint16_t)1 << relay_id)) != 0) {
                relay_ctrl_set_interlock_group(relay_id, group_id);
            }
        }
    }
    for (uint8_t cover_index = 0; cover_index < cover_clusters_cnt;
         cover_index++) {
        uint8_t  group_id = interlock_configs_cnt + cover_index + 1;
        uint16_t mask     = ((uint16_t)1 << cover_clusters[cover_index].open_relay_id) |
                            ((uint16_t)1 << cover_clusters[cover_index].close_relay_id);

        interlock_add_group(group_id, mask, 200);
        relay_ctrl_set_interlock_group(
            cover_clusters[cover_index].open_relay_id, group_id);
        relay_ctrl_set_interlock_group(
            cover_clusters[cover_index].close_relay_id, group_id);
    }
}

void feature_wiring_init(void) {
    button_input_init();
    switch_cluster_register_input();
    cover_switch_cluster_register_input();
    button_event_cluster_register_input();
    gesture_fsm_init();
    switch_cluster_register_gestures();
    cover_switch_cluster_register_gestures();
    action_mapper_init();
    button_event_cluster_register_gestures();

    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        uint8_t button_id = switch_clusters[i].button_id;

        button_configs[button_id].active_high =
            switch_clusters[i].mode ==
            ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY_NC;
        gesture_configs[button_id].hold_ms =
            switch_clusters[i].hold_duration_ms;
        gesture_configs[button_id].multi_click_gap_ms =
            switch_clusters[i].multi_click_gap_ms;
        button_configs[button_id].debounce_ms =
            switch_clusters[i].debounce_ms;
    }
    for (uint8_t i = 0; i < cover_switch_clusters_cnt; i++) {
        uint8_t open_id  = cover_switch_clusters[i].open_button_id;
        uint8_t close_id = cover_switch_clusters[i].close_button_id;

        gesture_configs[open_id].hold_ms =
            cover_switch_clusters[i].hold_duration_ms;
        gesture_configs[close_id].hold_ms =
            cover_switch_clusters[i].hold_duration_ms;
        gesture_configs[open_id].multi_click_gap_ms =
            cover_switch_clusters[i].multi_click_gap_ms;
        gesture_configs[close_id].multi_click_gap_ms =
            cover_switch_clusters[i].multi_click_gap_ms;
        button_configs[open_id].debounce_ms =
            cover_switch_clusters[i].debounce_ms;
        button_configs[close_id].debounce_ms =
            cover_switch_clusters[i].debounce_ms;
    }

    for (uint8_t i = 0; i < buttons_cnt; i++) {
        uint8_t button_id = button_input_add(&button_configs[i]);

        gesture_fsm_add(button_id, &gesture_configs[i]);
        action_mapper_add_reset_button(
            button_id, button_roles[i] == INPUT_BUTTON_ONBOARD);
    }
    button_event_cluster_sync_states();
}
