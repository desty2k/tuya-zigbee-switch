#include "feature_wiring.h"
#include "config_nv.h"
#include "device_config/config_parser.h"
#include "base_components/action_mapper.h"
#include "base_components/battery.h"
#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "base_components/indicator_feedback.h"
#include "base_components/interlock.h"
#include "base_components/led.h"
#include "base_components/network_indicator.h"
#include "base_components/relay_controller.h"
#include "base_components/relay_driver.h"
#include "hal/zigbee_ota.h"
#include "zigbee/basic_cluster.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/button_event_cluster.h"
#include "zigbee/cover_cluster.h"
#include "zigbee/cover_switch_cluster.h"
#include "zigbee/consts.h"
#include "zigbee/group_cluster.h"
#include "zigbee/poll_control_cluster.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/switch_cluster.h"
#include <string.h>

void parse_config(void) {
    device_composition_t c;

    device_config_read_from_nv();

    if (config_parser_parse((const char *)device_config_str.data,
                            &c) != CONFIG_PARSE_OK ||
        feature_wiring_build(&c) != FEATURE_WIRING_OK) {
        return;
    }
    feature_wiring_start();
}

network_indicator_t network_indicator = { .manual_state_when_connected = 1 };
led_t                            leds[5]; indicator_feedback_t indicator_feedbacks[5];
uint8_t                          leds_cnt;
button_config_t                  button_configs[11]; gesture_config_t gesture_configs[11];
input_button_role_t              button_roles[11]; uint8_t buttons_cnt;
relay_driver_t                   relay_drivers[10]; relay_config_t relay_configs[10];
uint8_t                          relays_cnt;
zigbee_switch_cluster            switch_clusters[10]; uint8_t switch_clusters_cnt;
zigbee_relay_cluster             relay_clusters[10]; uint8_t relay_clusters_cnt;
zigbee_cover_switch_cluster      cover_switch_clusters[3]; uint8_t cover_switch_clusters_cnt;
zigbee_cover_cluster             cover_clusters[3]; uint8_t cover_clusters_cnt;
uint8_t                          allow_simultaneous_latching_pulses;
battery_t                        battery = { .pin         = HAL_INVALID_PIN, .voltage_min = 2000,
                                             .voltage_max = 3000 };
hal_zigbee_endpoint              endpoints[10];
static device_interlock_config_t interlocks[10]; static uint8_t interlocks_cnt;
static zigbee_basic_cluster      basic_cluster = { .deviceEnable = 1 };
static zigbee_group_cluster      group_cluster; static hal_zigbee_cluster clusters[48];

static void network_changed(hal_zigbee_network_status_t s) {
    button_event_cluster_on_network_status_change(s); if (s == HAL_ZIGBEE_NETWORK_JOINED) {
        network_indicator_connected(&network_indicator); update_switch_clusters();
        update_relay_clusters();
    } else network_indicator_not_connected(&network_indicator);
}

static void endpoint_slots(const device_composition_t *c) {
    hal_zigbee_cluster *p = clusters; size_t left = 48; for (uint8_t i = 0; i < c->endpoint_count;
                                                             i++) {
        uint8_t slots = i == 0 ? (uint8_t)(2 + (battery.pin != HAL_INVALID_PIN)) : 0;
#ifdef END_DEVICE
        if (i == 0) {
            slots++;
        }
#endif
        if (i < switch_clusters_cnt) slots += 5;
        else if (i < switch_clusters_cnt + relay_clusters_cnt) slots += 3;
        else if (i <
                 switch_clusters_cnt + relay_clusters_cnt + cover_switch_clusters_cnt) slots += 4;
        else {
            slots++;
        }
        endpoints[i].endpoint   = i + 1;
        endpoints[i].profile_id = 0x0104; endpoints[i].device_id = 0xffff;
        endpoints[i].clusters   = p; endpoints[i].cluster_capacity = slots; p += slots;
        left -= slots;
    }

    (void)left;
}

feature_wiring_result_t feature_wiring_build(const device_composition_t *c) {
    if (!c || c->endpoint_count > 10) return FEATURE_WIRING_INVALID;

    memset(leds, 0, sizeof(leds)); memset(indicator_feedbacks, 0, sizeof(indicator_feedbacks));
    memset(button_configs, 0, sizeof(button_configs));
    memset(gesture_configs, 0, sizeof(gesture_configs));
    memset(relay_drivers, 0, sizeof(relay_drivers));
    memset(relay_configs, 0, sizeof(relay_configs));
    memset(switch_clusters, 0, sizeof(switch_clusters));
    memset(relay_clusters, 0, sizeof(relay_clusters));
    memset(cover_switch_clusters, 0, sizeof(cover_switch_clusters));
    memset(cover_clusters, 0, sizeof(cover_clusters)); memset(endpoints, 0, sizeof(endpoints));
    memset(clusters, 0, sizeof(clusters));
    leds_cnt                  = c->indicator_count; buttons_cnt = c->button_count;
    relays_cnt                = c->relay_count;
    switch_clusters_cnt       = c->switch_count; relay_clusters_cnt = c->relay_endpoint_count;
    cover_switch_clusters_cnt = c->cover_switch_count; cover_clusters_cnt = c->cover_count;
    interlocks_cnt            = c->interlock_count;
    memcpy(interlocks, c->interlocks, c->interlock_count * sizeof(*interlocks));
    allow_simultaneous_latching_pulses = c->simultaneous_latching_pulses;
    battery.pin = c->battery_pin;
    basic_cluster.manuName[0] = strlen(c->manufacturer);
    memcpy(basic_cluster.manuName + 1, c->manufacturer, basic_cluster.manuName[0]);
    basic_cluster.modelId[0] = strlen(c->model);
    memcpy(basic_cluster.modelId + 1, c->model, basic_cluster.modelId[0]);
    for (uint8_t i = 0; i < leds_cnt; i++) {
        hal_gpio_init(c->indicators[i].pin, 0, HAL_GPIO_PULL_NONE);
        leds[i].pin = c->indicators[i].pin; leds[i].on_high = c->indicators[i].on_high;
        indicator_feedback_init(&indicator_feedbacks[i], &leds[i]);
        if (c->indicators[i].dedicated) {
            network_indicator.indicators[0]     = &indicator_feedbacks[i];
            network_indicator.has_dedicated_led = 1;
        } else if (!network_indicator.has_dedicated_led) {
            for (uint8_t slot = 0; slot < 4; slot++) {
                if (network_indicator.indicators[slot] == NULL) {
                    network_indicator.indicators[slot] = &indicator_feedbacks[i];
                    break;
                }
            }
        }
    }
    for (uint8_t i = 0; i < buttons_cnt; i++) {
        hal_gpio_init(c->buttons[i].pin, 1, c->buttons[i].pull);
        button_configs[i] = (button_config_t){ c->buttons[i].pin,
                                               c->buttons[i].pull == HAL_GPIO_PULL_DOWN,
                                               c->buttons[i].pull == HAL_GPIO_PULL_DOWN,
                                               c->buttons[i].debounce_ms };
        gesture_configs[i] = (gesture_config_t){ .hold_ms            = c->buttons[i].hold_ms,
                                                 .multi_click_gap_ms =
                                                     c->buttons[i].multi_click_gap_ms };
        button_roles[i] = (input_button_role_t)c->buttons[i].role;
    }
    for (uint8_t i = 0; i < relays_cnt; i++) {
        hal_gpio_init(c->relays[i].on_pin, 0, HAL_GPIO_PULL_NONE);
        if (c->relays[i].is_latching) hal_gpio_init(c->relays[i].off_pin, 0, HAL_GPIO_PULL_NONE);
        relay_drivers[i].on_pin      = c->relays[i].on_pin;
        relay_drivers[i].off_pin     = c->relays[i].off_pin; relay_drivers[i].on_high = 1;
        relay_drivers[i].is_latching = c->relays[i].is_latching;
    }
    for (uint8_t i = 0; i < relay_clusters_cnt; i++) {
        uint8_t relay_id = c->relay_endpoint_ids[i];

        relay_clusters[i].relay_idx = i;
        relay_clusters[i].relay_id  = relay_id;
    }
    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        switch_clusters[i].switch_idx  = i; switch_clusters[i].button_id = c->switches[i].button_id;
        switch_clusters[i].relay_index = c->switches[i].relay_id + 1;
        switch_clusters[i].mode        =
            c->momentary_switches ? ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY :
            ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE;
        switch_clusters[i].action             = ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE;
        switch_clusters[i].relay_mode         = ZCL_ONOFF_CONFIGURATION_RELAY_MODE_SHORT;
        switch_clusters[i].binded_mode        = ZCL_ONOFF_CONFIGURATION_BINDED_MODE_SHORT;
        switch_clusters[i].hold_duration_ms   = 800;
        switch_clusters[i].multi_click_gap_ms =
            c->buttons[c->switches[i].button_id].multi_click_gap_ms;
        switch_clusters[i].debounce_ms     = c->buttons[c->switches[i].button_id].debounce_ms;
        switch_clusters[i].level_move_rate = 50;
    }
    for (uint8_t i = 0; i < leds_cnt; i++) {
        if (c->indicators[i].dedicated) {
            continue;
        }
        for (uint8_t relay_index = 0; relay_index < relay_clusters_cnt; relay_index++) {
            if (relay_clusters[relay_index].indicator_led == NULL) {
                relay_clusters[relay_index].indicator_led = &indicator_feedbacks[i];
                break;
            }
        }
        for (uint8_t switch_index = 0; switch_index < switch_clusters_cnt; switch_index++) {
            if (switch_clusters[switch_index].indicator_led == NULL) {
                switch_clusters[switch_index].indicator_led = &indicator_feedbacks[i];
                break;
            }
        }
    }
    for (uint8_t i = 0; i < cover_switch_clusters_cnt; i++) {
        cover_switch_clusters[i].cover_switch_idx   = i;
        cover_switch_clusters[i].open_button_id     = c->cover_switches[i].open_button_id;
        cover_switch_clusters[i].close_button_id    = c->cover_switches[i].close_button_id;
        cover_switch_clusters[i].hold_duration_ms   = 800;
        cover_switch_clusters[i].multi_click_gap_ms =
            c->buttons[c->cover_switches[i].open_button_id].multi_click_gap_ms;
        cover_switch_clusters[i].debounce_ms =
            c->buttons[c->cover_switches[i].open_button_id].debounce_ms;
    }
    for (uint8_t i = 0; i < cover_clusters_cnt; i++) {
        cover_clusters[i].cover_idx      = i;
        cover_clusters[i].open_relay_id  = c->covers[i].open_relay_id;
        cover_clusters[i].close_relay_id = c->covers[i].close_relay_id;
    }
    relay_ctrl_init();
    for (uint8_t i = 0; i < relays_cnt;
         i++) if (relay_ctrl_add(&relay_drivers[i],
                                 &relay_configs[i]) == UINT8_MAX) return FEATURE_WIRING_INVALID;

    for (uint8_t i = 0; i < interlocks_cnt; i++) {
        uint8_t g = i + 1;
        interlock_add_group(g, interlocks[i].relay_mask, interlocks[i].dead_time_ms);
        for (uint8_t r = 0;
             r < relays_cnt;
             r++) if (interlocks[i].relay_mask & ((uint16_t)1 <<
                                                  r)) relay_ctrl_set_interlock_group(r, g);
    }
    for (uint8_t i = 0; i < cover_clusters_cnt; i++) {
        uint8_t  g    = interlocks_cnt + i + 1;
        uint16_t mask = ((uint16_t)1 << cover_clusters[i].open_relay_id) | ((uint16_t)1 <<
                                                                            cover_clusters[i].
                                                                            close_relay_id);
        interlock_add_group(g, mask, 200);
        relay_ctrl_set_interlock_group(cover_clusters[i].open_relay_id, g);
        relay_ctrl_set_interlock_group(cover_clusters[i].close_relay_id, g);
    }
    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        if (switch_clusters[i].relay_index > relay_clusters_cnt) {
            switch_clusters[i].relay_mode =
                ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED;
            switch_clusters[i].relay_index = 0;
        }
    }
    button_event_cluster_init();
    endpoint_slots(c); basic_cluster_add_to_endpoint(&basic_cluster, &endpoints[0]);
    { hal_zigbee_cluster ota; hal_ota_cluster_setup(&ota);
      hal_zigbee_endpoint_add_cluster(&endpoints[0], endpoints[0].cluster_capacity, &ota);
    } if (battery.pin != HAL_INVALID_PIN) {
        static zigbee_battery_cluster b;

        battery_init(&battery);
        battery_cluster_add_to_endpoint(&b, &endpoints[0]);
    }
#ifdef END_DEVICE
    {
        static zigbee_poll_control_cluster poll_control;

        poll_control_cluster_add_to_endpoint(&poll_control, &endpoints[0],
                                             battery.pin != HAL_INVALID_PIN);
    }
#endif
    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        switch_cluster_add_to_endpoint(&switch_clusters[i], &endpoints[i]);
    }
    for (uint8_t i = 0; i < relay_clusters_cnt; i++) {
        uint8_t e = switch_clusters_cnt + i;
        relay_cluster_add_to_endpoint(&relay_clusters[i], &endpoints[e]);
        group_cluster_add_to_endpoint(&group_cluster, &endpoints[e]);
    }
    for (uint8_t i = 0; i < cover_switch_clusters_cnt;
         i++) cover_switch_cluster_add_to_endpoint(&cover_switch_clusters[i],
                                                   &endpoints[switch_clusters_cnt +
                                                              relay_clusters_cnt + i]);
    for (uint8_t i = 0;
         i < cover_clusters_cnt;
         i++) cover_cluster_add_to_endpoint(&cover_clusters[i],
                                            &endpoints[switch_clusters_cnt + relay_clusters_cnt +
                                                       cover_switch_clusters_cnt + i]);
    return FEATURE_WIRING_OK;
}

void feature_wiring_start(void) {
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
            button_configs[button_id].electrical_active_high !=
            (switch_clusters[i].mode ==
             ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY_NC);
        gesture_configs[button_id].hold_ms            = switch_clusters[i].hold_duration_ms;
        gesture_configs[button_id].multi_click_gap_ms =
            switch_clusters[i].multi_click_gap_ms;
        button_configs[button_id].debounce_ms = switch_clusters[i].debounce_ms;
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
        uint8_t id = button_input_add(&button_configs[i]); gesture_fsm_add(id, &gesture_configs[i]);
        action_mapper_add_reset_button(id, button_roles[i] == INPUT_BUTTON_ONBOARD);
    }
    button_event_cluster_sync_states();
    hal_register_on_network_status_change_callback(network_changed);
    network_changed(hal_zigbee_get_network_status());
    hal_zigbee_init(endpoints,
                    switch_clusters_cnt + relay_clusters_cnt + cover_switch_clusters_cnt +
                    cover_clusters_cnt ? switch_clusters_cnt + relay_clusters_cnt +
                    cover_switch_clusters_cnt + cover_clusters_cnt : 1);
}
