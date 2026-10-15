#include "feature_wiring.h"
#include "capability_state.h"
#include "config_nv.h"
#include "device_config/build_features.h"
#include "device_config/capability_state.h"
#include "device_config/config_parser.h"
#include "device_config/device_config_service.h"
#include "device_config/nvm_items.h"
#include "base_components/action_mapper.h"
#include "base_components/battery.h"
#include "base_components/button_input.h"
#include "base_components/child_lock.h"
#include "base_components/cover_controller.h"
#include "base_components/diagnostics_service.h"
#include "base_components/gesture_fsm.h"
#if FEATURE_PROBE
#include "base_components/gpio_probe_service.h"
#endif
#include "base_components/indicator_feedback.h"
#include "base_components/interlock.h"
#include "base_components/led.h"
#if FEATURE_METER
#include "base_components/energy_measurement/bl0937.h"
#include "base_components/meter_service.h"
#endif
#include "base_components/network_indicator.h"
#include "base_components/relay_controller.h"
#include "base_components/relay_driver.h"
#include "hal/zigbee_ota.h"
#include "zigbee/basic_cluster.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/button_event_cluster.h"
#include "zigbee/cover_cluster.h"
#include "zigbee/cover_switch_cluster.h"
#include "zigbee/device_config_cluster.h"
#include "zigbee/device_diagnostics_cluster.h"
#if FEATURE_METER
#include "zigbee/electrical_measurement_cluster.h"
#include "zigbee/meter_calibration_cluster.h"
#include "zigbee/metering_cluster.h"
#endif
#include "zigbee/zcl_cluster_defs.h"
#include "zigbee/zcl_ids.h"
#include "zigbee/group_cluster.h"
#include "zigbee/poll_control_cluster.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/switch_cluster.h"
#include <string.h>

#if DEVICE_COMPOSITION_MAX_RELAYS > RELAY_CONTROLLER_MAX_RELAYS
#error "a parsed relay layout must fit the relay controller"
#endif
#if DEVICE_COMPOSITION_MAX_BUTTONS > BUTTON_INPUT_MAX
#error "a parsed button layout must fit button input"
#endif
#if DEVICE_COMPOSITION_MAX_COVERS > COVER_CONTROLLER_MAX_COVERS
#error "a parsed cover layout must fit the cover controller"
#endif
#if FEATURE_METER && FEATURE_PROBE
#error "the meter and the GPIO probe share the TLSR8258 GPIO-to-RISC sources"
#endif
#if FEATURE_METER && DEVICE_COMPOSITION_MAX_METERS > METER_SERVICE_MAX_INSTANCES
#error "a parsed meter layout must fit the meter service"
#endif

/* Switch endpoint defaults until the switch config is loaded from NVM. */
#define SWITCH_DEFAULT_HOLD_MS            800
#define SWITCH_DEFAULT_LEVEL_MOVE_RATE    50 /* units per second */

/* Wired endpoints use no standard HA device type. */
#define WIRED_ENDPOINT_DEVICE_ID          0xffff

#if FEATURE_METER

/*
 * BL0937 scale of the BSEED TS011F outlet boards (one pulse per second on the
 * channel equals this much):
 *
 * - voltage: 120670 uV/Hz, measured on _TZ3210_drzlxjne (CF1 at 1964 Hz with
 *   SEL high while the stock firmware reported 237 V);
 * - current 9050 uA/Hz and power 1063500 uW/Hz: the sibling
 *   _TZ3000_b28wrpvx, measured against a 734.7 W resistive load (237 V,
 *   3.10 A; tuya-zigbee-switch PR #491). Its voltage scale (118000 uV/Hz)
 *   is within 2 % of the drzlxjne measurement, so both boards share the
 *   divider and shunt.
 *
 * The datasheet transfer functions (FCF = 1721506 Vv Vi / Vref^2, FCF1 =
 * 15397 Vv / Vref and 94638 Vi / Vref) give 924000 uW/Hz from the voltage
 * and current scales, 13 % below the measured power scale, so a unit needs a
 * reference-meter calibration (Meter Calibration gains) for better accuracy.
 */
#define METER_BL0937_VOLTAGE_UV_PER_HZ      120670u
#define METER_BL0937_CURRENT_UA_PER_HZ      9050u
#define METER_BL0937_POWER_UW_PER_HZ        1063500u

#define METER_SAMPLE_INTERVAL_MS            1000u

/*
 * Energy checkpoints (meter_service.c checkpoint_if_due): at most every 5 min,
 * after 50 Wh or once an hour with any energy. An abrupt power cut loses at
 * most max(50 Wh, 5 min of load), i.e. 307 Wh at 3680 W. The worst case of
 * 288 writes a day of a ~48-byte NV record rolls the 4 KB sectors of the app
 * NV module about 3.4 times a day: about 6200 erases per sector in ten
 * years, against the 100000-cycle flash endurance.
 */
#define METER_CHECKPOINT_MIN_INTERVAL_MS    (5u * 60u * 1000u)
#define METER_CHECKPOINT_MAX_INTERVAL_MS    (60u * 60u * 1000u)
#define METER_CHECKPOINT_ENERGY_UWH         50000000u

static bl0937_t meter_drivers[DEVICE_COMPOSITION_MAX_METERS];
static uint8_t  meter_ids[DEVICE_COMPOSITION_MAX_METERS];
static zigbee_electrical_measurement_cluster
    electrical_measurement_clusters[DEVICE_COMPOSITION_MAX_METERS];
static zigbee_metering_cluster          metering_clusters[DEVICE_COMPOSITION_MAX_METERS];
static zigbee_meter_calibration_cluster meter_calibration_clusters[DEVICE_COMPOSITION_MAX_METERS];
static uint8_t meters_cnt;
#endif

#define NETWORK_INDICATOR_SLOTS                           \
        (sizeof(((network_indicator_t *)0)->indicators) / \
         sizeof(((network_indicator_t *)0)->indicators[0]))

void parse_config(void) {
    device_composition_t  c;
    config_parse_result_t result = config_parser_parse(device_config_service_config(), &c);

    if (result != CONFIG_PARSE_OK) {
        device_config_service_set_error(result == CONFIG_PARSE_FEATURE_MISSING ?
                                        DEVICE_CONFIG_ERROR_FEATURE_MISSING :
                                        DEVICE_CONFIG_ERROR_PARSE);
        feature_wiring_build_recovery();
        return;
    }
    if (feature_wiring_build(&c) != FEATURE_WIRING_OK) {
        device_config_service_set_error(DEVICE_CONFIG_ERROR_RESOURCE);
        feature_wiring_build_recovery();
        return;
    }
    feature_wiring_start();
}

network_indicator_t network_indicator = { .manual_state_when_connected = 1 };
led_t leds[DEVICE_COMPOSITION_MAX_INDICATORS];
static indicator_feedback_t indicator_feedbacks[DEVICE_COMPOSITION_MAX_INDICATORS];
static led_t lock_led;
static indicator_feedback_t lock_led_feedback;
uint8_t leds_cnt;
static button_config_t     wired_button_configs[DEVICE_COMPOSITION_MAX_BUTTONS];
static gesture_config_t    wired_gesture_configs[DEVICE_COMPOSITION_MAX_BUTTONS];
static input_button_role_t button_roles[DEVICE_COMPOSITION_MAX_BUTTONS];
uint8_t               buttons_cnt;
relay_driver_t        relay_drivers[DEVICE_COMPOSITION_MAX_RELAYS];
static relay_config_t relay_configs[DEVICE_COMPOSITION_MAX_RELAYS];
uint8_t               relays_cnt;
/* Endpoint counts per kind; a kind whose feature is not built stays at 0. */
uint8_t        switch_clusters_cnt;
uint8_t        relay_clusters_cnt;
uint8_t        cover_switch_clusters_cnt;
static uint8_t cover_clusters_cnt;
#if FEATURE_SWITCH
zigbee_switch_cluster switch_clusters[DEVICE_COMPOSITION_MAX_SWITCHES];
#endif
#if FEATURE_RELAY
zigbee_relay_cluster        relay_clusters[DEVICE_COMPOSITION_MAX_RELAYS];
static zigbee_group_cluster group_cluster;
#endif
#if FEATURE_COVER
zigbee_cover_switch_cluster
    cover_switch_clusters[DEVICE_COMPOSITION_MAX_COVER_SWITCHES];
static zigbee_cover_cluster      cover_clusters[DEVICE_COMPOSITION_MAX_COVERS];
static cover_controller_config_t cover_controller_configs[DEVICE_COMPOSITION_MAX_COVERS];
#endif
uint8_t             allow_simultaneous_latching_pulses;
battery_t           battery = { .pin         = HAL_INVALID_PIN,
                                .voltage_min =            2000,
                                .voltage_max = 3000 };
hal_zigbee_endpoint endpoints[DEVICE_COMPOSITION_MAX_ENDPOINTS];
static device_interlock_config_t interlocks[DEVICE_COMPOSITION_MAX_INTERLOCKS];
static uint8_t interlocks_cnt;
static zigbee_basic_cluster                basic_cluster = { .deviceEnable = 1 };
static zigbee_device_config_cluster_t      config_cluster;
static zigbee_device_diagnostics_cluster_t diagnostics_cluster;
static hal_zigbee_cluster clusters[HAL_ZIGBEE_MAX_CLUSTERS];

/* Clusters each endpoint kind adds. endpoint_slots() reserves them and the
 * preflight counts them; the add_to_endpoint functions add exactly these. */
enum {
    EP1_BASE_CLUSTERS     = 4, /* Basic, device config, diagnostics, OTA */
    SWITCH_CLUSTERS       = 5, /* config, OnOff/Level clients, multistate, button events */
    RELAY_CLUSTERS        = 3, /* OnOff, Level, Groups */
    COVER_SWITCH_CLUSTERS = 4, /* config, WindowCovering client, multistate, button events */
    COVER_CLUSTERS        = 1, /* WindowCovering */
    METER_CLUSTERS        = 3, /* Electrical Measurement, Metering, Meter Calibration */
};

#define ATTRIBUTE_COUNT(type, member) \
        ((uint16_t)(sizeof(((type *)0)->member) / sizeof(hal_zigbee_attribute)))

#if FEATURE_PROBE
static void probe_claim(hal_gpio_pin_t pin) {
    if (pin != HAL_INVALID_PIN) {
        hal_gpio_probe_id_t id = hal_gpio_probe_id_from_pin(pin);

        gpio_probe_service_claim(HAL_GPIO_PROBE_PORT(id),
                                 HAL_GPIO_PROBE_INDEX(id));
    }
}

/* Every pin the composition gives a feature stays out of the probe. */
static void probe_claim_composition(const device_composition_t *c) {
    gpio_probe_service_init();
    for (uint8_t i = 0; i < c->button_count; i++) {
        probe_claim(c->buttons[i].pin);
    }
    for (uint8_t i = 0; i < c->relay_count; i++) {
        probe_claim(c->relays[i].on_pin);
        if (c->relays[i].is_latching) {
            probe_claim(c->relays[i].off_pin);
        }
    }
    for (uint8_t i = 0; i < c->indicator_count; i++) {
        probe_claim(c->indicators[i].pin);
    }
    probe_claim(c->battery_pin);
    probe_claim(c->lock_led_pin);
}

#endif

static void apply_status_led_mode(void *context,
                                  device_config_led_mode_t mode) {
    network_indicator_t *indicator = context;

    if (indicator == NULL) return;

    indicator->manual_state_when_connected = mode != DEVICE_CONFIG_LED_OFF;
    network_indicator_from_manual_state(indicator);
}

static void network_changed(hal_zigbee_network_status_t s) {
    button_event_cluster_on_network_status_change(s);
    if (s == HAL_ZIGBEE_NETWORK_JOINED) {
        network_indicator_connected(&network_indicator);
#if FEATURE_SWITCH
        update_switch_clusters();
#endif
#if FEATURE_RELAY
        update_relay_clusters();
#endif
    } else {
        network_indicator_not_connected(&network_indicator);
    }
}

static void add_ota_cluster(void) {
    hal_zigbee_cluster ota;

    hal_ota_cluster_setup(&ota);
    hal_zigbee_endpoint_add_cluster(&endpoints[0], endpoints[0].cluster_capacity,
                                    &ota);
}

static void endpoint_slots(const device_composition_t *c) {
    hal_zigbee_cluster *p                  = clusters;
    uint8_t             switches_end       = switch_clusters_cnt;
    uint8_t             relays_end         = switches_end + relay_clusters_cnt;
    uint8_t             cover_switches_end = relays_end + cover_switch_clusters_cnt;
    uint8_t             covers_end         = cover_switches_end + cover_clusters_cnt;

    for (uint8_t i = 0; i < c->endpoint_count; i++) {
        uint8_t slots = 0;

        if (i == 0) {
            slots = EP1_BASE_CLUSTERS;
            if (battery.pin != HAL_INVALID_PIN) {
                slots++;
            }
#ifdef END_DEVICE
            slots++;
#endif
        }
        if (i < switches_end) {
            slots += SWITCH_CLUSTERS;
        } else if (i < relays_end) {
            slots += RELAY_CLUSTERS;
        } else if (i < cover_switches_end) {
            slots += COVER_SWITCH_CLUSTERS;
        } else if (i < covers_end) {
            slots += COVER_CLUSTERS;
        }
#if FEATURE_METER
        if (i < meters_cnt) {
            slots += METER_CLUSTERS;
        }
#endif
        endpoints[i].endpoint         = i + 1;
        endpoints[i].profile_id       = ZCL_HA_PROFILE;
        endpoints[i].device_id        = WIRED_ENDPOINT_DEVICE_ID;
        endpoints[i].clusters         = p;
        endpoints[i].cluster_capacity = slots;
        p += slots;
    }
}

static uint8_t built_endpoint_count(void) {
    uint8_t count = switch_clusters_cnt + relay_clusters_cnt +
                    cover_switch_clusters_cnt + cover_clusters_cnt;

    return count == 0 ? 1 : count;
}

#if FEATURE_RELAY || FEATURE_SWITCH || FEATURE_COVER

/* The n-th indicator that is not the dedicated status LED, or NULL. Relay
 * and switch endpoints take shared indicators in endpoint order; covers take
 * the next ones, two each (see wire_cover_leds). */
static indicator_feedback_t *shared_indicator(const device_composition_t *c, uint8_t n) {
    for (uint8_t i = 0; i < leds_cnt; i++) {
        if (c->indicators[i].dedicated) {
            continue;
        }
        if (n == 0) {
            return &indicator_feedbacks[i];
        }
        n--;
    }
    return NULL;
}

#endif

#if FEATURE_RELAY
static void wire_relay_clusters(const device_composition_t *c) {
    memset(relay_clusters, 0, sizeof(relay_clusters));
    relay_cluster_config_interlock_count = c->interlock_count;
    for (uint8_t i = 0; i < relay_clusters_cnt; i++) {
        relay_clusters[i].relay_idx     = i;
        relay_clusters[i].relay_id      = c->relay_endpoint_ids[i];
        relay_clusters[i].indicator_led = shared_indicator(c, i);
    }
}

#endif

#if FEATURE_SWITCH
static void wire_switch_clusters(const device_composition_t *c) {
    memset(switch_clusters, 0, sizeof(switch_clusters));
    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        const device_button_config_t *button = &c->buttons[c->switches[i].button_id];

        switch_clusters[i].switch_idx  = i;
        switch_clusters[i].button_id   = c->switches[i].button_id;
        switch_clusters[i].relay_index = c->switches[i].relay_id + 1;
        switch_clusters[i].mode        =
            c->momentary_switches ? ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY :
            ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE;
        switch_clusters[i].action             = ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE;
        switch_clusters[i].relay_mode         = ZCL_ONOFF_CONFIGURATION_RELAY_MODE_SHORT;
        switch_clusters[i].binded_mode        = ZCL_ONOFF_CONFIGURATION_BINDED_MODE_SHORT;
        switch_clusters[i].hold_duration_ms   = SWITCH_DEFAULT_HOLD_MS;
        switch_clusters[i].multi_click_gap_ms = button->multi_click_gap_ms;
        switch_clusters[i].debounce_ms        = button->debounce_ms;
        switch_clusters[i].level_move_rate    = SWITCH_DEFAULT_LEVEL_MOVE_RATE;
        switch_clusters[i].indicator_led      = shared_indicator(c, i);
        if (switch_clusters[i].relay_index > relay_clusters_cnt) {
            switch_clusters[i].relay_mode  = ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED;
            switch_clusters[i].relay_index = 0;
        }
    }
}

#endif

/* `K` groups take ids 1..interlocks_cnt in config order. */
static feature_wiring_result_t wire_interlocks(void) {
    for (uint8_t i = 0; i < interlocks_cnt; i++) {
        uint8_t g = i + 1;

        if (!interlock_add_group(g, interlocks[i].dead_time_ms)) {
            return FEATURE_WIRING_INVALID;
        }
        for (uint8_t r = 0; r < relays_cnt; r++) {
            if ((interlocks[i].relay_mask & ((uint16_t)1 << r)) != 0 &&
                !interlock_set_relay_group(r, g)) {
                return FEATURE_WIRING_INVALID;
            }
        }
    }
    return FEATURE_WIRING_OK;
}

#if FEATURE_COVER

typedef struct {
    indicator_feedback_t *open_led;
    indicator_feedback_t *close_led;
} cover_leds_t;

static cover_leds_t cover_leds[DEVICE_COMPOSITION_MAX_COVERS];

static void cover_leds_on_state(void *context, uint8_t cover_id,
                                const cover_state_t *state) {
    cover_leds_t *pair = context;

    (void)cover_id;
    if (pair->open_led != NULL) {
        indicator_feedback_set_base_state(pair->open_led, state->motion == COVER_OPENING);
    }
    if (pair->close_led != NULL) {
        indicator_feedback_set_base_state(pair->close_led, state->motion == COVER_CLOSING);
    }
}

/* Each cover lights its first indicator while opening and its second while
 * closing, after the indicators the relay and switch endpoints take. */
static bool wire_cover_leds(const device_composition_t *c) {
    uint8_t first = 0;

#if FEATURE_RELAY
    first = relay_clusters_cnt;
#endif
#if FEATURE_SWITCH
    if (switch_clusters_cnt > first) {
        first = switch_clusters_cnt;
    }
#endif
    for (uint8_t i = 0; i < cover_clusters_cnt; i++) {
        cover_leds[i].open_led  = shared_indicator(c, (uint8_t)(first + 2 * i));
        cover_leds[i].close_led = shared_indicator(c, (uint8_t)(first + 2 * i + 1));
        if ((cover_leds[i].open_led != NULL || cover_leds[i].close_led != NULL) &&
            !cover_controller_subscribe(cover_clusters[i].controller_id,
                                        cover_leds_on_state, &cover_leds[i])) {
            return false;
        }
    }
    return true;
}

/* Cover relays form one interlock group per cover, numbered after the
 * config's K groups. */
static feature_wiring_result_t wire_covers(const device_composition_t *c) {
    memset(cover_switch_clusters, 0, sizeof(cover_switch_clusters));
    memset(cover_clusters, 0, sizeof(cover_clusters));
    cover_controller_init();
    for (uint8_t i = 0; i < cover_switch_clusters_cnt; i++) {
        const device_button_config_t *open_button =
            &c->buttons[c->cover_switches[i].open_button_id];

        cover_switch_clusters[i].cover_switch_idx   = i;
        cover_switch_clusters[i].open_button_id     = c->cover_switches[i].open_button_id;
        cover_switch_clusters[i].close_button_id    = c->cover_switches[i].close_button_id;
        cover_switch_clusters[i].stop_button_id     = c->cover_switches[i].stop_button_id;
        cover_switch_clusters[i].hold_duration_ms   = COVER_SWITCH_DEFAULT_HOLD_MS;
        cover_switch_clusters[i].multi_click_gap_ms = open_button->multi_click_gap_ms;
        cover_switch_clusters[i].debounce_ms        = open_button->debounce_ms;
    }
    for (uint8_t i = 0; i < cover_clusters_cnt; i++) {
        uint8_t g = interlocks_cnt + i + 1;

        cover_controller_configs[i] = (cover_controller_config_t){
            .open_relay_id     = c->covers[i].open_relay_id,
            .close_relay_id    = c->covers[i].close_relay_id,
            .manual_max_run_ms = COVER_DEFAULT_MANUAL_MAX_RUN_MS,
            .nvm_item          = NV_ITEM_COVER_STATE(i),
        };
        if (!interlock_add_group(g, DEVICE_COMPOSITION_DEFAULT_DEAD_TIME_MS) ||
            !interlock_set_relay_group(c->covers[i].open_relay_id, g) ||
            !interlock_set_relay_group(c->covers[i].close_relay_id, g)) {
            return FEATURE_WIRING_INVALID;
        }
    }
    for (uint8_t i = 0; i < cover_clusters_cnt; i++) {
        cover_clusters[i].controller_id = cover_controller_add(&cover_controller_configs[i]);
        if (cover_clusters[i].controller_id == UINT8_MAX) {
            return FEATURE_WIRING_INVALID;
        }
    }
    return wire_cover_leds(c) ? FEATURE_WIRING_OK : FEATURE_WIRING_INVALID;
}

#endif

#if FEATURE_METER

/* Meter i reports on endpoint i + 1; validation keeps meters within the
 * endpoint count. */
static feature_wiring_result_t wire_meters(const device_composition_t *c) {
    meter_service_init();
    for (uint8_t i = 0; i < meters_cnt; i++) {
        meter_drivers[i].cf_counter  = HAL_GPIO_COUNTER_INVALID_ID;
        meter_drivers[i].cf1_counter = HAL_GPIO_COUNTER_INVALID_ID;
        meter_ids[i] = UINT8_MAX;
    }
    for (uint8_t i = 0; i < meters_cnt; i++) {
        const device_meter_config_t *meter  = &c->meters[i];
        const bl0937_config_t        config = {
            .cf_pin  = meter->cf_pin,
            .cf1_pin = meter->cf1_pin,
            .sel_pin = meter->sel_pin,
            .sel_high_selects_voltage = meter->sel_high_selects_voltage,
            .voltage_uv_per_hz        = METER_BL0937_VOLTAGE_UV_PER_HZ,
            .current_ua_per_hz        = METER_BL0937_CURRENT_UA_PER_HZ,
            .power_uw_per_hz          = METER_BL0937_POWER_UW_PER_HZ,
        };
        meter_service_config_t       service = {
            .sample_interval_ms         = METER_SAMPLE_INTERVAL_MS,
            .checkpoint_min_interval_ms = METER_CHECKPOINT_MIN_INTERVAL_MS,
            .checkpoint_max_interval_ms = METER_CHECKPOINT_MAX_INTERVAL_MS,
            .checkpoint_energy_uwh      = METER_CHECKPOINT_ENERGY_UWH,
            .nvm_item                   = NV_ITEM_METER_STATE(i),
        };

        if (!bl0937_init(&meter_drivers[i], &config)) {
            return FEATURE_WIRING_INVALID;
        }
        service.driver = bl0937_as_meter(&meter_drivers[i]);
        meter_ids[i]   = meter_service_add(&service);
        if (meter_ids[i] == UINT8_MAX) {
            return FEATURE_WIRING_INVALID;
        }
    }
    return FEATURE_WIRING_OK;
}

static void stop_meters(void) {
    for (uint8_t i = 0; i < meters_cnt; i++) {
        meter_service_stop(meter_ids[i]);
        bl0937_close(&meter_drivers[i]);
    }
    meter_service_init();
}

#endif

/* The `P` LED blinks the child lock feedback. On the pin of an `L` or `I`
 * LED it shares that LED's feedback, so a blink is layered over the LED's
 * state and restores it. Without `P` the status LED is used, else the first
 * indicator, else no LED. */
static indicator_feedback_t *wire_lock_led(const device_composition_t *c) {
    for (uint8_t i = 0; i < leds_cnt; i++) {
        if (c->lock_led_pin != HAL_INVALID_PIN && c->indicators[i].pin == c->lock_led_pin) {
            return &indicator_feedbacks[i];
        }
    }
    if (c->lock_led_pin != HAL_INVALID_PIN) {
        hal_gpio_init(c->lock_led_pin, 0, HAL_GPIO_PULL_NONE);
        lock_led = (led_t){ .pin = c->lock_led_pin, .on_high = c->lock_led_on_high };
        indicator_feedback_init(&lock_led_feedback, &lock_led);
        return &lock_led_feedback;
    }
    for (uint8_t i = 0; i < leds_cnt; i++) {
        if (c->indicators[i].dedicated) {
            return &indicator_feedbacks[i];
        }
    }
    return leds_cnt > 0 ? &indicator_feedbacks[0] : NULL;
}

feature_wiring_result_t feature_wiring_build(const device_composition_t *c) {
    if (feature_wiring_preflight(c) != FEATURE_WIRING_OK) {
        return FEATURE_WIRING_INVALID;
    }
#if FEATURE_PROBE
    probe_claim_composition(c);
#endif

    memset(leds, 0, sizeof(leds));
    memset(indicator_feedbacks, 0, sizeof(indicator_feedbacks));
    memset(wired_button_configs, 0, sizeof(wired_button_configs));
    memset(wired_gesture_configs, 0, sizeof(wired_gesture_configs));
    memset(relay_drivers, 0, sizeof(relay_drivers));
    memset(relay_configs, 0, sizeof(relay_configs));
    memset(endpoints, 0, sizeof(endpoints));
    memset(clusters, 0, sizeof(clusters));
    memset(network_indicator.indicators, 0, sizeof(network_indicator.indicators));
    network_indicator.has_dedicated_led = 0;
    leds_cnt                  = c->indicator_count;
    buttons_cnt               = c->button_count;
    relays_cnt                = c->relay_count;
    switch_clusters_cnt       = c->switch_count;
    relay_clusters_cnt        = c->relay_endpoint_count;
    cover_switch_clusters_cnt = c->cover_switch_count;
    cover_clusters_cnt        = c->cover_count;
#if FEATURE_METER
    meters_cnt = c->meter_count;
#endif
    interlocks_cnt = c->interlock_count;
    memcpy(interlocks, c->interlocks, c->interlock_count * sizeof(*interlocks));
    allow_simultaneous_latching_pulses = c->simultaneous_latching_pulses;
    battery.pin = c->battery_pin;
    if (c->has_image_type) {
        hal_zigbee_set_image_type(c->image_type);
    }
    basic_cluster.manuName[0] = strlen(c->manufacturer);
    memcpy(basic_cluster.manuName + 1, c->manufacturer, basic_cluster.manuName[0]);
    basic_cluster.modelId[0] = strlen(c->model);
    memcpy(basic_cluster.modelId + 1, c->model, basic_cluster.modelId[0]);
    for (uint8_t i = 0; i < relays_cnt; i++) {
        hal_gpio_init(c->relays[i].on_pin, 0, HAL_GPIO_PULL_NONE);
        if (c->relays[i].is_latching) {
            hal_gpio_init(c->relays[i].off_pin, 0, HAL_GPIO_PULL_NONE);
        }
        relay_drivers[i].on_pin      = c->relays[i].on_pin;
        relay_drivers[i].off_pin     = c->relays[i].off_pin;
        relay_drivers[i].on_high     = 1;
        relay_drivers[i].is_latching = c->relays[i].is_latching;
    }
    relay_ctrl_init();
    for (uint8_t i = 0; i < relays_cnt; i++) {
        if (relay_ctrl_add(&relay_drivers[i], &relay_configs[i]) == UINT8_MAX) {
            return FEATURE_WIRING_INVALID;
        }
    }

    for (uint8_t i = 0; i < leds_cnt; i++) {
        hal_gpio_init(c->indicators[i].pin, 0, HAL_GPIO_PULL_NONE);
        leds[i].pin     = c->indicators[i].pin;
        leds[i].on_high = c->indicators[i].on_high;
        indicator_feedback_init(&indicator_feedbacks[i], &leds[i]);
        if (c->indicators[i].dedicated) {
            network_indicator.indicators[0]     = &indicator_feedbacks[i];
            network_indicator.has_dedicated_led = 1;
        } else if (!network_indicator.has_dedicated_led) {
            for (uint8_t slot = 0; slot < NETWORK_INDICATOR_SLOTS; slot++) {
                if (network_indicator.indicators[slot] == NULL) {
                    network_indicator.indicators[slot] = &indicator_feedbacks[i];
                    break;
                }
            }
        }
    }
    device_config_service_set_led_mode_provider(
        network_indicator.has_dedicated_led ? apply_status_led_mode : NULL,
        network_indicator.has_dedicated_led ? &network_indicator : NULL);
    child_lock_reset_wiring();
    child_lock_set_indicator(wire_lock_led(c));
    for (uint8_t i = 0; i < buttons_cnt; i++) {
        bool pulled_down = c->buttons[i].pull == HAL_GPIO_PULL_DOWN;

        hal_gpio_init(c->buttons[i].pin, 1, c->buttons[i].pull);
        wired_button_configs[i] = (button_config_t){
            .pin                    = c->buttons[i].pin,
            .active_high            = pulled_down,
            .electrical_active_high = pulled_down,
            .debounce_ms            = c->buttons[i].debounce_ms,
        };
        wired_gesture_configs[i] = (gesture_config_t){
            .hold_ms            = c->buttons[i].hold_ms,
            .multi_click_gap_ms = c->buttons[i].multi_click_gap_ms,
        };
        button_roles[i] = (input_button_role_t)c->buttons[i].role;
    }
#if FEATURE_RELAY
    wire_relay_clusters(c);
#endif
#if FEATURE_SWITCH
    wire_switch_clusters(c);
#endif
    if (wire_interlocks() != FEATURE_WIRING_OK) {
        return FEATURE_WIRING_INVALID;
    }
#if FEATURE_COVER
    if (wire_covers(c) != FEATURE_WIRING_OK) {
        return FEATURE_WIRING_INVALID;
    }
#endif
#if FEATURE_METER
    if (wire_meters(c) != FEATURE_WIRING_OK) {
        return FEATURE_WIRING_INVALID;
    }
#endif
    button_event_cluster_init();
    endpoint_slots(c);
    basic_cluster_add_to_endpoint(&basic_cluster, &endpoints[0]);
    device_config_cluster_init(&config_cluster);
    device_config_cluster_add_to_endpoint(&config_cluster, &endpoints[0]);
    device_diagnostics_cluster_init(&diagnostics_cluster);
    device_diagnostics_cluster_add_to_endpoint(&diagnostics_cluster, &endpoints[0]);
    add_ota_cluster();
#if FEATURE_BATTERY
    if (battery.pin != HAL_INVALID_PIN) {
        static zigbee_battery_cluster b;

        battery_init(&battery);
        battery_cluster_add_to_endpoint(&b, &endpoints[0]);
    }
#endif
#ifdef END_DEVICE
    {
        static zigbee_poll_control_cluster poll_control;

        poll_control_cluster_add_to_endpoint(&poll_control, &endpoints[0],
                                             battery.pin != HAL_INVALID_PIN);
    }
#endif
#if FEATURE_SWITCH
    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        switch_cluster_add_to_endpoint(&switch_clusters[i], &endpoints[i]);
    }
#endif
#if FEATURE_RELAY
    capability_state_sync_relay_topology(device_composition_relay_topology(c));
    for (uint8_t i = 0; i < relay_clusters_cnt; i++) {
        uint8_t e = switch_clusters_cnt + i;

        relay_cluster_add_to_endpoint(&relay_clusters[i], &endpoints[e]);
        group_cluster_add_to_endpoint(&group_cluster, &endpoints[e]);
    }
#endif
#if FEATURE_COVER
    for (uint8_t i = 0; i < cover_switch_clusters_cnt; i++) {
        uint8_t e = switch_clusters_cnt + relay_clusters_cnt + i;

        cover_switch_cluster_add_to_endpoint(&cover_switch_clusters[i],
                                             &endpoints[e]);
    }
    for (uint8_t i = 0; i < cover_clusters_cnt; i++) {
        uint8_t e = switch_clusters_cnt + relay_clusters_cnt +
                    cover_switch_clusters_cnt + i;

        cover_cluster_add_to_endpoint(&cover_clusters[i], &endpoints[e]);
    }
#endif
#if FEATURE_METER
    for (uint8_t i = 0; i < meters_cnt; i++) {
        electrical_measurement_cluster_add_to_endpoint(&electrical_measurement_clusters[i],
                                                       meter_ids[i], &endpoints[i]);
        metering_cluster_add_to_endpoint(&metering_clusters[i], meter_ids[i], &endpoints[i]);
        meter_calibration_cluster_add_to_endpoint(&meter_calibration_clusters[i], meter_ids[i],
                                                  &endpoints[i]);
    }
#endif
    return hal_zigbee_validate_platform_graph(endpoints, built_endpoint_count()) ==
           HAL_ZIGBEE_DESCRIPTOR_OK ? FEATURE_WIRING_OK : FEATURE_WIRING_INVALID;
}

void feature_wiring_graph_usage(const device_composition_t *c,
                                hal_zigbee_graph_usage_t *usage) {
    hal_zigbee_cluster ota;
    uint8_t            shared_indicators = 0;
    uint8_t            relays_with_indicator;

    *usage = (hal_zigbee_graph_usage_t){ .endpoints = c->endpoint_count };
    hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_BASIC, HAL_ZIGBEE_ROLE_SERVER,
                                       ATTRIBUTE_COUNT(zigbee_basic_cluster, attr_infos));
    hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_DEVICE_CONFIG,
                                       HAL_ZIGBEE_ROLE_SERVER,
                                       ATTRIBUTE_COUNT(zigbee_device_config_cluster_t,
                                                       attr_infos));
    hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_DEVICE_DIAGNOSTICS,
                                       HAL_ZIGBEE_ROLE_SERVER,
                                       ATTRIBUTE_COUNT(zigbee_device_diagnostics_cluster_t,
                                                       attr_infos));
    hal_ota_cluster_setup(&ota);
    hal_zigbee_graph_usage_add_cluster(usage, ota.cluster_id, ota.role, ota.attribute_count);
    if (c->battery_pin != HAL_INVALID_PIN) {
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_POWER_CFG,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_battery_cluster, attr_infos));
    }
#ifdef END_DEVICE
    hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_POLL_CONTROL, HAL_ZIGBEE_ROLE_SERVER,
                                       ATTRIBUTE_COUNT(zigbee_poll_control_cluster,
                                                       attr_infos));
#endif
    for (uint8_t i = 0; i < c->switch_count; i++) {
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_switch_cluster, attr_infos));
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_ON_OFF, HAL_ZIGBEE_ROLE_CLIENT, 0);
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_switch_cluster,
                                                           multistate_attr_infos));
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_BUTTON_EVENT,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_button_event_cluster,
                                                           attr_infos));
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_LEVEL_CONTROL, HAL_ZIGBEE_ROLE_CLIENT,
                                           0);
    }
    for (uint8_t i = 0; i < c->indicator_count; i++) {
        if (!c->indicators[i].dedicated) {
            shared_indicators++;
        }
    }
    relays_with_indicator = shared_indicators < c->relay_endpoint_count ?
                            shared_indicators : c->relay_endpoint_count;
    for (uint8_t i = 0; i < c->relay_endpoint_count; i++) {
        uint16_t on_off_attributes = ATTRIBUTE_COUNT(zigbee_relay_cluster, attr_infos);

        if (i >= relays_with_indicator) {
            on_off_attributes -= RELAY_CLUSTER_INDICATOR_ATTRIBUTES;
        }
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_ON_OFF, HAL_ZIGBEE_ROLE_SERVER,
                                           on_off_attributes);
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_LEVEL_CONTROL,
                                           HAL_ZIGBEE_ROLE_SERVER, 0);
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_GROUPS, HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_group_cluster, attr_infos));
    }
    for (uint8_t i = 0; i < c->cover_switch_count; i++) {
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_COVER_SWITCH_CONFIG,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_cover_switch_cluster,
                                                           config_attr_infos));
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_WINDOW_COVERING,
                                           HAL_ZIGBEE_ROLE_CLIENT, 0);
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_cover_switch_cluster,
                                                           multistate_attr_infos));
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_BUTTON_EVENT,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_button_event_cluster,
                                                           attr_infos));
    }
    for (uint8_t i = 0; i < c->cover_count; i++) {
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_WINDOW_COVERING,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_cover_cluster, attr_infos));
    }
#if FEATURE_METER
    for (uint8_t i = 0; i < c->meter_count; i++) {
        hal_zigbee_graph_usage_add_cluster(
            usage, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT, HAL_ZIGBEE_ROLE_SERVER,
            ATTRIBUTE_COUNT(zigbee_electrical_measurement_cluster, attr_infos));
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_METERING, HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_metering_cluster,
                                                           attr_infos));
        hal_zigbee_graph_usage_add_cluster(usage, ZCL_CLUSTER_METER_CALIBRATION,
                                           HAL_ZIGBEE_ROLE_SERVER,
                                           ATTRIBUTE_COUNT(zigbee_meter_calibration_cluster,
                                                           attr_infos));
    }
#endif
}

feature_wiring_result_t feature_wiring_preflight(const device_composition_t *c) {
    hal_zigbee_graph_usage_t usage;

    if (device_composition_validate(c) != CONFIG_PARSE_OK) {
        return FEATURE_WIRING_INVALID;
    }
    feature_wiring_graph_usage(c, &usage);
    return hal_zigbee_check_graph_usage(&usage) == HAL_ZIGBEE_DESCRIPTOR_OK ?
           FEATURE_WIRING_OK : FEATURE_WIRING_INVALID;
}

/*
 * A graph that cannot run leaves the device as safe as a config that failed
 * to parse: every relay off, no input action and no cover movement. Relays
 * are switched off through their drivers because the controller is reset
 * afterwards; pending deferred switch-ons are cancelled first.
 */
static void stop_wired_features(void) {
    for (uint8_t i = 0; i < relays_cnt; i++) {
        relay_ctrl_cancel_deferred_on(i);
        relay_driver_apply(&relay_drivers[i], false);
    }
    relay_ctrl_init();
#if FEATURE_COVER
    cover_controller_init();
#endif
#if FEATURE_METER
    stop_meters();
#endif
    button_input_init();
    gesture_fsm_init();
    action_mapper_init();
#if FEATURE_METER
    meters_cnt = 0;
#endif
    relays_cnt                = 0;
    buttons_cnt               = 0;
    switch_clusters_cnt       = 0;
    relay_clusters_cnt        = 0;
    cover_switch_clusters_cnt = 0;
    cover_clusters_cnt        = 0;
}

void feature_wiring_build_recovery(void) {
    stop_wired_features();
#if FEATURE_PROBE
    /*
     * Which pins a failed graph drives is unknown, so the probe stays off;
     * every pin still rests on the pull-down, so no relay driver floats.
     */
    gpio_probe_service_init();
    gpio_probe_service_rest_pins();
#endif
    memset(endpoints, 0, sizeof(endpoints));
    memset(clusters, 0, sizeof(clusters));
    memset(&basic_cluster, 0, sizeof(basic_cluster));
    device_config_service_set_led_mode_provider(NULL, NULL);
    child_lock_reset_wiring();
    basic_cluster.deviceEnable = 1;
    basic_cluster.manuName[0]  = 7;
    memcpy(basic_cluster.manuName + 1, "unknown", 7);
    basic_cluster.modelId[0] = 8;
    memcpy(basic_cluster.modelId + 1, "recovery", 8);
    endpoints[0].endpoint         = 1;
    endpoints[0].profile_id       = ZCL_HA_PROFILE;
    endpoints[0].device_id        = WIRED_ENDPOINT_DEVICE_ID;
    endpoints[0].clusters         = clusters;
    endpoints[0].cluster_capacity = EP1_BASE_CLUSTERS;
    basic_cluster_add_to_endpoint(&basic_cluster, &endpoints[0]);
    device_config_cluster_init(&config_cluster);
    device_config_cluster_add_to_endpoint(&config_cluster, &endpoints[0]);
    device_diagnostics_cluster_init(&diagnostics_cluster);
    device_diagnostics_cluster_add_to_endpoint(&diagnostics_cluster, &endpoints[0]);
    add_ota_cluster();
    hal_zigbee_init(endpoints, 1);
    hal_register_on_network_status_change_callback(network_changed);
}

/* Lock gestures: open and close of each momentary cover switch, and the
 * first two switch buttons, or the only one, while they are momentary. A
 * toggle input stays down while its load is on, so it never locks. Onboard
 * buttons have none: their 2 s hold is the factory reset. */
#if FEATURE_COVER
static bool cover_switch_lock_gesture_enabled(void *context) {
    const zigbee_cover_switch_cluster *cluster = context;

    return cluster->switch_type != ZCL_COVER_SWITCH_TYPE_TOGGLE;
}

#endif
#if FEATURE_SWITCH
static bool switch_lock_gesture_enabled(void *context) {
    (void)context;
    for (uint8_t i = 0; i < switch_clusters_cnt && i < 2; i++) {
        if (switch_clusters[i].mode == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE) {
            return false;
        }
    }
    return true;
}

#endif

static void wire_child_lock_gestures(void) {
#if FEATURE_COVER
    for (uint8_t i = 0; i < cover_switch_clusters_cnt; i++) {
        (void)child_lock_add_gesture(cover_switch_clusters[i].open_button_id,
                                     cover_switch_clusters[i].close_button_id,
                                     cover_switch_lock_gesture_enabled, &cover_switch_clusters[i]);
    }
#endif
#if FEATURE_SWITCH
    if (switch_clusters_cnt >= 2) {
        (void)child_lock_add_gesture(switch_clusters[0].button_id, switch_clusters[1].button_id,
                                     switch_lock_gesture_enabled, NULL);
    } else if (switch_clusters_cnt == 1) {
        (void)child_lock_add_gesture(switch_clusters[0].button_id, CHILD_LOCK_NO_BUTTON,
                                     switch_lock_gesture_enabled, NULL);
    }
#endif
}

void feature_wiring_start(void) {
    button_input_init();
#if FEATURE_SWITCH
    switch_cluster_register_input();
#endif
#if FEATURE_COVER
    cover_switch_cluster_register_input();
#endif
    button_event_cluster_register_input();
    child_lock_register_input();
    gesture_fsm_init();
#if FEATURE_SWITCH
    switch_cluster_register_gestures();
#endif
#if FEATURE_COVER
    cover_switch_cluster_register_gestures();
#endif
    action_mapper_init();
    action_mapper_set_sinks(&(action_mapper_sinks_t){
#if FEATURE_COVER
        .cover = cover_controller_action,
#endif
        .factory_reset = device_config_service_factory_reset,
    });
    button_event_cluster_register_gestures();

#if FEATURE_SWITCH
    for (uint8_t i = 0; i < switch_clusters_cnt; i++) {
        uint8_t button_id = switch_clusters[i].button_id;

        wired_button_configs[button_id].active_high =
            wired_button_configs[button_id].electrical_active_high !=
            (switch_clusters[i].mode ==
             ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY_NC);
        wired_gesture_configs[button_id].hold_ms            = switch_clusters[i].hold_duration_ms;
        wired_gesture_configs[button_id].multi_click_gap_ms =
            switch_clusters[i].multi_click_gap_ms;
        wired_button_configs[button_id].debounce_ms = switch_clusters[i].debounce_ms;
    }
#endif
#if FEATURE_COVER
    for (uint8_t i = 0; i < cover_switch_clusters_cnt; i++) {
        uint8_t open_id  = cover_switch_clusters[i].open_button_id;
        uint8_t close_id = cover_switch_clusters[i].close_button_id;

        wired_gesture_configs[open_id].hold_ms =
            cover_switch_clusters[i].hold_duration_ms;
        wired_gesture_configs[close_id].hold_ms =
            cover_switch_clusters[i].hold_duration_ms;
        wired_gesture_configs[open_id].multi_click_gap_ms =
            cover_switch_clusters[i].multi_click_gap_ms;
        wired_gesture_configs[close_id].multi_click_gap_ms =
            cover_switch_clusters[i].multi_click_gap_ms;
        wired_button_configs[open_id].debounce_ms =
            cover_switch_clusters[i].debounce_ms;
        wired_button_configs[close_id].debounce_ms =
            cover_switch_clusters[i].debounce_ms;
        if (cover_switch_clusters[i].stop_button_id != DEVICE_COMPOSITION_NO_BUTTON) {
            wired_button_configs[cover_switch_clusters[i].stop_button_id].debounce_ms =
                cover_switch_clusters[i].debounce_ms;
        }
    }
#endif
    for (uint8_t i = 0; i < buttons_cnt; i++) {
        uint8_t id = button_input_add(&wired_button_configs[i]);

        gesture_fsm_add(id, &wired_gesture_configs[i]);
        action_mapper_add_reset_button(id, button_roles[i] == INPUT_BUTTON_ONBOARD);
    }
    wire_child_lock_gestures();
    button_event_cluster_sync_states();
    if (!hal_zigbee_init(endpoints, built_endpoint_count())) {
        device_config_service_set_error(DEVICE_CONFIG_ERROR_RESOURCE);
        feature_wiring_build_recovery();
        return;
    }
    hal_register_on_network_status_change_callback(network_changed);
    network_changed(hal_zigbee_get_network_status());
#if FEATURE_METER
    for (uint8_t i = 0; i < meters_cnt; i++) {
        meter_service_start(meter_ids[i]);
    }
#endif
#if FEATURE_PROBE
    gpio_probe_service_enable();
#endif
}
