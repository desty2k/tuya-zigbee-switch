#ifndef FEATURE_WIRING_H_
#define FEATURE_WIRING_H_

#include "base_components/battery.h"
#include "base_components/led.h"
#include "base_components/network_indicator.h"
#include "base_components/relay_driver.h"
#include "device_config/device_composition.h"
#include "hal/zigbee.h"

#include <stdint.h>

typedef enum {
    FEATURE_WIRING_OK,
    FEATURE_WIRING_INVALID,
} feature_wiring_result_t;

typedef enum {
    INPUT_BUTTON_ONBOARD,
    INPUT_BUTTON_SWITCH,
    INPUT_BUTTON_COVER_SWITCH,
} input_button_role_t;

extern network_indicator_t network_indicator;
extern hal_zigbee_endpoint endpoints[DEVICE_COMPOSITION_MAX_ENDPOINTS];
extern uint8_t             allow_simultaneous_latching_pulses;
extern battery_t           battery;
extern led_t          leds[DEVICE_COMPOSITION_MAX_INDICATORS];
extern uint8_t        leds_cnt;
extern uint8_t        buttons_cnt;
extern relay_driver_t relay_drivers[DEVICE_COMPOSITION_MAX_RELAYS];
extern uint8_t        relays_cnt;

void parse_config(void);
void feature_wiring_build_recovery(void);

/** Platform resources the graph built from `composition` takes. */
void feature_wiring_graph_usage(const device_composition_t *composition,
                                hal_zigbee_graph_usage_t *usage);

/** Accept a composition only if its graph fits the shared platform limits. */
feature_wiring_result_t feature_wiring_preflight(
    const device_composition_t *composition);
feature_wiring_result_t feature_wiring_build(
    const device_composition_t *composition);
void feature_wiring_start(void);

#endif
