#include "feature_wiring.h"

#include "base_components/action_mapper.h"
#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "base_components/relay_controller.h"
#include "base_components/relay_driver.h"
#include "zigbee/cover_switch_cluster.h"
#include "zigbee/consts.h"
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
extern uint8_t                     relays_cnt;

void feature_wiring_init_relays(void) {
    relay_ctrl_init();
    for (uint8_t i = 0; i < relays_cnt; i++) {
        relay_ctrl_add(&relay_drivers[i]);
    }
}

void feature_wiring_init(void) {
    button_input_init();
    switch_cluster_register_input();
    cover_switch_cluster_register_input();
    gesture_fsm_init();
    switch_cluster_register_gestures();
    cover_switch_cluster_register_gestures();
    action_mapper_init();

    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        uint8_t button_id = switch_clusters[i].button_id;

        button_configs[button_id].active_high =
            switch_clusters[i].mode ==
            ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY_NC;
        gesture_configs[button_id].hold_ms =
            switch_clusters[i].hold_duration_ms;
    }
    for (uint8_t i = 0; i < cover_switch_clusters_cnt; i++) {
        uint8_t open_id  = cover_switch_clusters[i].open_button_id;
        uint8_t close_id = cover_switch_clusters[i].close_button_id;

        gesture_configs[open_id].hold_ms =
            cover_switch_clusters[i].hold_duration_ms;
        gesture_configs[close_id].hold_ms =
            cover_switch_clusters[i].hold_duration_ms;
    }

    for (uint8_t i = 0; i < buttons_cnt; i++) {
        uint8_t button_id = button_input_add(&button_configs[i]);

        gesture_fsm_add(button_id, &gesture_configs[i]);
        action_mapper_add_reset_button(
            button_id, button_roles[i] == INPUT_BUTTON_ONBOARD);
    }
}
