#include "hal/gpio.h"
#include "hal/printf_selector.h"
#include "hal/zigbee.h"
#include "zigbee/basic_cluster.h"
#include "zigbee/button_event_cluster.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/consts.h"
#include "zigbee/cover_cluster.h"
#include "zigbee/cover_switch_cluster.h"
#include "zigbee/group_cluster.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/poll_control_cluster.h"
#include "zigbee/switch_cluster.h"

#include <stdint.h>
#include <string.h>

#include "base_components/indicator_feedback.h"
#include "base_components/led.h"
#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "base_components/network_indicator.h"
#include "base_components/battery.h"
#include "base_components/relay_controller.h"
#include "base_components/relay_driver.h"
#include "base_components/interlock.h"
#include "config_nv.h"
#include "device_config/device_params_nv.h"
#include "device_config/reset.h"
#include "device_config/feature_wiring.h"
#include "hal/zigbee.h"
#include "hal/zigbee_ota.h"

// Forward declarations
void peripherals_init(void);

// extern ota_preamble_t baseEndpoint_otaInfo;

network_indicator_t network_indicator = {
    .indicators                  = { NULL, NULL, NULL, NULL },
    .has_dedicated_led           = 0,
    .manual_state_when_connected = 1,
};

led_t leds[5];
indicator_feedback_t indicator_feedbacks[5];
uint8_t leds_cnt = 0;

button_config_t     button_configs[11];
gesture_config_t    gesture_configs[11];
input_button_role_t button_roles[11];
uint8_t             buttons_cnt = 0;

relay_driver_t relay_drivers[10];
relay_config_t relay_configs[10];
uint8_t        relays_cnt = 0;

device_interlock_config_t interlock_configs[INTERLOCK_MAX_GROUPS];
uint8_t interlock_configs_cnt = 0;

zigbee_basic_cluster basic_cluster = {
    .deviceEnable = 1,
};

zigbee_group_cluster group_cluster = {};

zigbee_switch_cluster switch_clusters[4];
uint8_t switch_clusters_cnt = 0;

zigbee_relay_cluster relay_clusters[4];
uint8_t relay_clusters_cnt = 0;

zigbee_cover_switch_cluster cover_switch_clusters[3];
uint8_t cover_switch_clusters_cnt = 0;

zigbee_cover_cluster cover_clusters[3];
uint8_t cover_clusters_cnt = 0;

hal_zigbee_cluster  clusters[48];
hal_zigbee_endpoint endpoints[10];

uint8_t allow_simultaneous_latching_pulses = 0;

battery_t battery = {
    .pin         = HAL_INVALID_PIN,
    .voltage_min =            2000,
    .voltage_max =            3000,
};

uint32_t parse_int(const char *s);
static uint16_t config_parser_parse_hex(const char *s);
char *seek_until(char *cursor, char needle);
char *extract_next_entry(char **cursor);

void parse_config() {
    device_config_read_from_nv();
    char *cursor = (char *)device_config_str.data;

    const char *zb_manufacturer = extract_next_entry(&cursor);

    basic_cluster.manuName[0] = strlen(zb_manufacturer);
    if (basic_cluster.manuName[0] > 31) {
        printf("Manufacturer too big\r\n");
        reset_all();
    }
    memcpy(basic_cluster.manuName + 1, zb_manufacturer,
           basic_cluster.manuName[0]);

    const char *zb_model = extract_next_entry(&cursor);
    basic_cluster.modelId[0] = strlen(zb_model);
    if (basic_cluster.modelId[0] > 31) {
        printf("Model too big\r\n");
        reset_all();
    }
    memcpy(basic_cluster.modelId + 1, zb_model, basic_cluster.modelId[0]);

    bool     has_dedicated_status_led = false;
    uint16_t debounce_ms        = BUTTON_DEBOUNCE_DEFAULT_MS;
    uint16_t multi_click_gap_ms = 350;
    char *   entry;
    for (entry = extract_next_entry(&cursor); *entry != '\0';
         entry = extract_next_entry(&cursor)) {
        if (entry[0] == 'S' && entry[1] == 'L' && entry[2] == 'P') {
            // Simultaneous Latching Pulses == SLP
            allow_simultaneous_latching_pulses = 1;
        } else if (entry[0] == 'D' && entry[1] >= '0' && entry[1] <= '9') {
            // D<N> sets the global debounce duration in milliseconds.
            debounce_ms = (uint16_t)parse_int(entry + 1);
            for (int i = 0; i < buttons_cnt; i++) {
                button_configs[i].debounce_ms = debounce_ms;
            }
        } else if (entry[0] == 'G' && entry[1] >= '0' && entry[1] <= '9') {
            multi_click_gap_ms = (uint16_t)parse_int(entry + 1);
            for (int i = 0; i < buttons_cnt; i++) {
                gesture_configs[i].multi_click_gap_ms = multi_click_gap_ms;
            }
        } else if (entry[0] == 'K' && interlock_configs_cnt < INTERLOCK_MAX_GROUPS) {
            char *separator = seek_until(entry + 1, ':');

            interlock_configs[interlock_configs_cnt].relay_mask =
                config_parser_parse_hex(entry + 1);
            interlock_configs[interlock_configs_cnt].dead_time_ms =
                *separator == ':' ? (uint16_t)parse_int(separator + 1) : 0;
            if (interlock_configs[interlock_configs_cnt].relay_mask != 0) {
                interlock_configs_cnt++;
            }
        } else if (entry[0] == 'B' && entry[1] == 'T') {
            // Battery: BT<pin>, e.g. BTC5
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 2);
            battery.pin = pin;
            battery_init(&battery);
        } else if (entry[0] == 'B') {
            hal_gpio_pin_t  pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pull_t pull = hal_gpio_parse_pull(entry + 3);
            hal_gpio_init(pin, 1, pull);

            button_configs[buttons_cnt].pin                 = pin;
            button_configs[buttons_cnt].active_high         = 0;
            button_configs[buttons_cnt].debounce_ms         = debounce_ms;
            gesture_configs[buttons_cnt].hold_ms            = 2000;
            gesture_configs[buttons_cnt].multi_click_gap_ms =
                multi_click_gap_ms;
            button_roles[buttons_cnt] = INPUT_BUTTON_ONBOARD;
            buttons_cnt++;
        } else if (entry[0] == 'L') {
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 1);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
            leds[leds_cnt].pin     = pin;
            leds[leds_cnt].on_high = entry[3] != 'i';
            indicator_feedback_init(&indicator_feedbacks[leds_cnt],
                                    &leds[leds_cnt]);

            network_indicator.indicators[0]     = &indicator_feedbacks[leds_cnt];
            network_indicator.indicators[1]     = NULL;
            network_indicator.has_dedicated_led = true;

            has_dedicated_status_led = true;
            leds_cnt++;
        } else if (entry[0] == 'I') {
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 1);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
            leds[leds_cnt].pin     = pin;
            leds[leds_cnt].on_high = entry[3] != 'i';
            indicator_feedback_init(&indicator_feedbacks[leds_cnt],
                                    &leds[leds_cnt]);

            for (int index = 0; index < 4; index++) {
                if (relay_clusters[index].indicator_led == NULL) {
                    relay_clusters[index].indicator_led =
                        &indicator_feedbacks[leds_cnt];
                    break;
                }
            }

            for (int index = 0; index < 4; index++) {
                if (switch_clusters[index].indicator_led == NULL) {
                    switch_clusters[index].indicator_led =
                        &indicator_feedbacks[leds_cnt];
                    break;
                }
            }

            if (!has_dedicated_status_led) {
                for (int index = 0; index < 4; index++) {
                    if (network_indicator.indicators[index] == NULL) {
                        network_indicator.indicators[index] =
                            &indicator_feedbacks[leds_cnt];
                        break;
                    }
                }
            }
            leds_cnt++;
        } else if (entry[0] == 'S') {
            hal_gpio_pin_t  pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pull_t pull = hal_gpio_parse_pull(entry + 3);
            hal_gpio_init(pin, 1, pull);

            button_configs[buttons_cnt].pin                 = pin;
            button_configs[buttons_cnt].active_high         = entry[3] == 'd';
            button_configs[buttons_cnt].debounce_ms         = debounce_ms;
            gesture_configs[buttons_cnt].hold_ms            = 800;
            gesture_configs[buttons_cnt].multi_click_gap_ms =
                multi_click_gap_ms;
            button_roles[buttons_cnt] = INPUT_BUTTON_SWITCH;
            switch_clusters[switch_clusters_cnt].switch_idx = switch_clusters_cnt;
            switch_clusters[switch_clusters_cnt].mode       =
                ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE;
            switch_clusters[switch_clusters_cnt].action =
                ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE;
            switch_clusters[switch_clusters_cnt].relay_mode =
                ZCL_ONOFF_CONFIGURATION_RELAY_MODE_SHORT;
            switch_clusters[switch_clusters_cnt].binded_mode =
                ZCL_ONOFF_CONFIGURATION_BINDED_MODE_SHORT;
            switch_clusters[switch_clusters_cnt].relay_index        = switch_clusters_cnt + 1;
            switch_clusters[switch_clusters_cnt].button_id          = buttons_cnt;
            switch_clusters[switch_clusters_cnt].hold_duration_ms   = 800;
            switch_clusters[switch_clusters_cnt].multi_click_gap_ms =
                multi_click_gap_ms;
            switch_clusters[switch_clusters_cnt].debounce_ms     = debounce_ms;
            switch_clusters[switch_clusters_cnt].level_move_rate = 50;
            buttons_cnt++;
            switch_clusters_cnt++;
        } else if (entry[0] == 'R') {
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 1);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);

            relay_drivers[relays_cnt].on_pin  = pin;
            relay_drivers[relays_cnt].on_high = 1;

            if (entry[3] != '\0') {
                pin = hal_gpio_parse_pin(entry + 3);
                hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
                relay_drivers[relays_cnt].off_pin     = pin;
                relay_drivers[relays_cnt].is_latching = 1;
            }

            relay_clusters[relay_clusters_cnt].relay_idx = relay_clusters_cnt;
            relay_clusters[relay_clusters_cnt].relay_id  = relays_cnt;

            relays_cnt++;
            relay_clusters_cnt++;
        } else if (entry[0] == 'X') {
            hal_gpio_pin_t  open_pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pin_t  close_pin = hal_gpio_parse_pin(entry + 3);
            hal_gpio_pull_t pull      = hal_gpio_parse_pull(entry + 5);

            hal_gpio_init(open_pin, 1, pull);
            hal_gpio_init(close_pin, 1, pull);

            button_configs[buttons_cnt].pin                 = open_pin;
            button_configs[buttons_cnt].active_high         = 0;
            button_configs[buttons_cnt].debounce_ms         = debounce_ms;
            gesture_configs[buttons_cnt].hold_ms            = 800;
            gesture_configs[buttons_cnt].multi_click_gap_ms =
                multi_click_gap_ms;
            button_roles[buttons_cnt] = INPUT_BUTTON_COVER_SWITCH;
            cover_switch_clusters[cover_switch_clusters_cnt].open_button_id =
                buttons_cnt++;

            button_configs[buttons_cnt].pin                 = close_pin;
            button_configs[buttons_cnt].active_high         = 0;
            button_configs[buttons_cnt].debounce_ms         = debounce_ms;
            gesture_configs[buttons_cnt].hold_ms            = 800;
            gesture_configs[buttons_cnt].multi_click_gap_ms =
                multi_click_gap_ms;
            button_roles[buttons_cnt] = INPUT_BUTTON_COVER_SWITCH;
            cover_switch_clusters[cover_switch_clusters_cnt].close_button_id =
                buttons_cnt++;
            cover_switch_clusters[cover_switch_clusters_cnt].hold_duration_ms =
                800;
            cover_switch_clusters[cover_switch_clusters_cnt].multi_click_gap_ms =
                multi_click_gap_ms;
            cover_switch_clusters[cover_switch_clusters_cnt].debounce_ms =
                debounce_ms;
            cover_switch_clusters[cover_switch_clusters_cnt].cover_switch_idx =
                cover_switch_clusters_cnt;
            cover_switch_clusters_cnt++;
        } else if (entry[0] == 'C') {
            hal_gpio_pin_t open_pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pin_t close_pin = hal_gpio_parse_pin(entry + 3);

            hal_gpio_init(open_pin, 0, HAL_GPIO_PULL_NONE);
            hal_gpio_init(close_pin, 0, HAL_GPIO_PULL_NONE);

            relay_drivers[relays_cnt].on_pin      = open_pin;
            relay_drivers[relays_cnt].on_high     = 1;
            relay_drivers[relays_cnt].is_latching = 0;
            uint8_t open_relay_id = relays_cnt++;

            relay_drivers[relays_cnt].on_pin      = close_pin;
            relay_drivers[relays_cnt].on_high     = 1;
            relay_drivers[relays_cnt].is_latching = 0;
            uint8_t close_relay_id = relays_cnt++;

            cover_clusters[cover_clusters_cnt].open_relay_id  = open_relay_id;
            cover_clusters[cover_clusters_cnt].close_relay_id = close_relay_id;
            cover_clusters[cover_clusters_cnt].cover_idx      = cover_clusters_cnt;
            cover_clusters_cnt++;
        } else if (entry[0] == 'i') {
            uint32_t image_type = parse_int(entry + 1);
            hal_zigbee_set_image_type(image_type);
        } else if (entry[0] == 'M') {
            for (int index = 0; index < switch_clusters_cnt; index++) {
                switch_clusters[index].mode =
                    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY;
            }
        }
    }

    peripherals_init();

    printf("Initializing Zigbee with %d switches, %d relays, %d cover switches, "
           "%d covers\r\n",
           switch_clusters_cnt, relay_clusters_cnt, cover_switch_clusters_cnt,
           cover_clusters_cnt);

    uint8_t total_endpoints = switch_clusters_cnt + relay_clusters_cnt +
                              cover_switch_clusters_cnt + cover_clusters_cnt;

    button_event_cluster_init();

    hal_zigbee_cluster *cluster_ptr = clusters;

    for (int index = 0; index < switch_clusters_cnt; index++) {
        if (switch_clusters[index].relay_index > relay_clusters_cnt) {
            // Detach switches that point past the available relay count.
            switch_clusters[index].relay_mode =
                ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED;
            switch_clusters[index].relay_index = 0;
        }
    }

    // special case when no switches or relays are defined, so we can init a
    // "clean" device and configure it while running endpoint 1 still needs to be
    // initialised even though wenn no switches or relays are defined, so it can
    // join the network!
    if (total_endpoints == 0)
        total_endpoints = 1;

    for (int index = 0; index < total_endpoints; index++) {
        endpoints[index].endpoint   = index + 1;
        endpoints[index].profile_id = 0x0104;
        endpoints[index].device_id  = 0xffff;
    }

    endpoints[0].clusters = cluster_ptr;
    basic_cluster_add_to_endpoint(&basic_cluster, &endpoints[0]);

    hal_ota_cluster_setup(&endpoints[0].clusters[endpoints[0].cluster_count]);
    endpoints[0].cluster_count++;

    // Add battery cluster for battery-powered devices
    if (battery.pin != HAL_INVALID_PIN) {
        static zigbee_battery_cluster battery_cluster;
        battery_cluster_add_to_endpoint(&battery_cluster, &endpoints[0]);
    }

#ifdef END_DEVICE
    // Add poll control cluster for end devices
    static zigbee_poll_control_cluster poll_ctrl_cluster;
    poll_control_cluster_add_to_endpoint(&poll_ctrl_cluster, &endpoints[0],
                                         battery.pin != HAL_INVALID_PIN);
#endif

    for (int index = 0; index < switch_clusters_cnt; index++) {
        if (index != 0) {
            cluster_ptr += endpoints[index - 1].cluster_count;
            endpoints[index].clusters = cluster_ptr;
        }
        switch_clusters[index].multi_click_gap_ms =
            gesture_configs[switch_clusters[index].button_id].multi_click_gap_ms;
        switch_clusters[index].debounce_ms =
            button_configs[switch_clusters[index].button_id].debounce_ms;
        switch_cluster_add_to_endpoint(&switch_clusters[index], &endpoints[index]);
    }
    for (int index = 0; index < relay_clusters_cnt; index++) {
        if (switch_clusters_cnt + index != 0) {
            cluster_ptr += endpoints[switch_clusters_cnt + index - 1].cluster_count;
            endpoints[switch_clusters_cnt + index].clusters = cluster_ptr;
        }
        relay_cluster_add_to_endpoint(&relay_clusters[index],
                                      &endpoints[switch_clusters_cnt + index]);
        // Group cluster is stateless, safe to add to multiple endpoints
        group_cluster_add_to_endpoint(&group_cluster,
                                      &endpoints[switch_clusters_cnt + index]);
    }

    int cover_switch_base = switch_clusters_cnt + relay_clusters_cnt;
    for (int index = 0; index < cover_switch_clusters_cnt; index++) {
        if (cover_switch_base + index != 0) {
            cluster_ptr += endpoints[cover_switch_base + index - 1].cluster_count;
            endpoints[cover_switch_base + index].clusters = cluster_ptr;
        }
        cover_switch_clusters[index].multi_click_gap_ms =
            gesture_configs[cover_switch_clusters[index].open_button_id]
            .multi_click_gap_ms;
        cover_switch_clusters[index].debounce_ms =
            button_configs[cover_switch_clusters[index].open_button_id]
            .debounce_ms;
        cover_switch_cluster_add_to_endpoint(&cover_switch_clusters[index],
                                             &endpoints[cover_switch_base + index]);
    }

    int cover_base =
        switch_clusters_cnt + relay_clusters_cnt + cover_switch_clusters_cnt;
    for (int index = 0; index < cover_clusters_cnt; index++) {
        if (cover_base + index != 0) {
            cluster_ptr += endpoints[cover_base + index - 1].cluster_count;
            endpoints[cover_base + index].clusters = cluster_ptr;
        }
        cover_cluster_add_to_endpoint(&cover_clusters[index],
                                      &endpoints[cover_base + index]);
    }

    feature_wiring_init();

    hal_zigbee_init(endpoints, total_endpoints);
    while (cursor != (char *)device_config_str.data) {
        cursor--;
        if (*cursor == '\0') {
            *cursor = ';';
        }
    }

    printf("Config parsed successfully\r\n");
}

void network_indicator_on_network_status_change(
    hal_zigbee_network_status_t new_status) {
    printf("Network status changed to %d\r\n", new_status);
    button_event_cluster_on_network_status_change(new_status);
    if (new_status == HAL_ZIGBEE_NETWORK_JOINED) {
        if (battery.pin != HAL_INVALID_PIN) {
            network_indicator.manual_state_when_connected = 0;
        }
        network_indicator_connected(&network_indicator);
        update_switch_clusters();
        update_relay_clusters();
    } else {
        network_indicator_not_connected(&network_indicator);
    }
}

void peripherals_init() {
    for (int index = 0; index < leds_cnt; index++) {
        indicator_feedback_init(&indicator_feedbacks[index], &leds[index]);
    }
    feature_wiring_init_relays();
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) {
        network_indicator_connected(&network_indicator);
        update_switch_clusters();
        update_relay_clusters();
    } else {
        network_indicator_not_connected(&network_indicator);
    }
    hal_register_on_network_status_change_callback(
        network_indicator_on_network_status_change);
}

// Helper functions

char *seek_until(char *cursor, char needle) {
    while (*cursor != needle && *cursor != '\0') {
        cursor++;
    }
    return(cursor);
}

char *extract_next_entry(char **cursor) {
    char *end = seek_until(*cursor, ';');

    *end = '\0';
    char *res = *cursor;
    *cursor = end + 1;
    return(res);
}

uint32_t parse_int(const char *s) {
    if (!s)
        return 0;

    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return n;
}

static uint16_t config_parser_parse_hex(const char *s) {
    uint16_t result = 0;

    while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') ||
           (*s >= 'A' && *s <= 'F')) {
        uint8_t digit = *s <= '9' ? (uint8_t)(*s - '0')
                                  : (uint8_t)((*s | 0x20) - 'a' + 10);

        result = (uint16_t)((result << 4) | digit);
        s++;
    }
    return result;
}
