#ifndef DEVICE_CONFIG_DEVICE_COMPOSITION_H_
#define DEVICE_CONFIG_DEVICE_COMPOSITION_H_

#include "hal/gpio.h"
#include "hal/zigbee_limits.h"
#include <stdbool.h>
#include <stdint.h>

#define DEVICE_COMPOSITION_MAX_RELAYS              10
#define DEVICE_COMPOSITION_MAX_BUTTONS             11
#define DEVICE_COMPOSITION_MAX_INDICATORS          5
#define DEVICE_COMPOSITION_MAX_COVERS              3
#define DEVICE_COMPOSITION_MAX_METERS              2
#define DEVICE_COMPOSITION_MAX_SWITCHES            10
#define DEVICE_COMPOSITION_MAX_COVER_SWITCHES      3
#define DEVICE_COMPOSITION_MAX_INTERLOCKS          10
#define DEVICE_COMPOSITION_MAX_ENDPOINTS           HAL_ZIGBEE_MAX_ENDPOINTS

/*
 * Dead time for groups that do not state one: the `K` token without `:ms`
 * and the implicit group of each cover's direction relays. A relay contact
 * opens within ~10-15 ms of de-energising, but the arc of an inductive load
 * and a reversing motor's back-EMF need longer to decay; 200 ms covers both
 * while staying below the delay a user notices on a switch press.
 */
#define DEVICE_COMPOSITION_DEFAULT_DEAD_TIME_MS    200

typedef enum { CONFIG_PARSE_OK, CONFIG_PARSE_SYNTAX, CONFIG_PARSE_LIMIT,
               CONFIG_PARSE_REFERENCE, CONFIG_PARSE_PIN_CONFLICT,
               CONFIG_PARSE_RESOURCE, CONFIG_PARSE_FEATURE_MISSING } config_parse_result_t;
typedef enum { DEVICE_BUTTON_ONBOARD, DEVICE_BUTTON_SWITCH,
               DEVICE_BUTTON_COVER_SWITCH } device_button_role_t;
typedef struct { hal_gpio_pin_t       pin; hal_gpio_pull_t pull; uint16_t debounce_ms;
                 uint16_t             hold_ms; uint16_t multi_click_gap_ms;
                 device_button_role_t role; } device_button_config_t;
typedef struct { hal_gpio_pin_t on_pin; hal_gpio_pin_t off_pin;
                 bool           is_latching; } device_relay_config_t;
typedef struct { hal_gpio_pin_t pin; bool on_high; bool dedicated; }   device_indicator_config_t;
typedef struct { uint16_t relay_mask; uint16_t dead_time_ms; }         device_interlock_config_t;
typedef struct { uint8_t open_relay_id; uint8_t close_relay_id; }      device_cover_config_t;
typedef enum { DEVICE_METER_BL0937 = 1 }                               device_meter_ic_t;

/* Pulse meter (`E` token): CF and CF1 take one counter route each; SEL is a
 * meter-owned output. */
typedef struct { device_meter_ic_t ic; hal_gpio_pin_t cf_pin; hal_gpio_pin_t cf1_pin;
                 hal_gpio_pin_t    sel_pin; bool sel_high_selects_voltage; }
device_meter_config_t;
typedef struct { uint8_t button_id; uint8_t relay_id; }               device_switch_config_t;
typedef struct { uint8_t open_button_id; uint8_t close_button_id; }   device_cover_switch_config_t;
typedef struct {
    char                         manufacturer[32]; char model[32];
    device_button_config_t       buttons[DEVICE_COMPOSITION_MAX_BUTTONS];
    device_relay_config_t        relays[DEVICE_COMPOSITION_MAX_RELAYS];
    device_indicator_config_t    indicators[DEVICE_COMPOSITION_MAX_INDICATORS];
    device_interlock_config_t    interlocks[DEVICE_COMPOSITION_MAX_INTERLOCKS];
    device_cover_config_t        covers[DEVICE_COMPOSITION_MAX_COVERS];
    device_meter_config_t        meters[DEVICE_COMPOSITION_MAX_METERS];
    device_switch_config_t       switches[DEVICE_COMPOSITION_MAX_SWITCHES];
    device_cover_switch_config_t cover_switches[DEVICE_COMPOSITION_MAX_COVER_SWITCHES];
    uint8_t                      relay_endpoint_ids[DEVICE_COMPOSITION_MAX_RELAYS];
    hal_gpio_pin_t               battery_pin;
    uint16_t                     image_type; // OTA image type of the `i` token
    uint8_t                      relay_count, relay_endpoint_count, button_count, indicator_count,
                                 interlock_count;
    uint8_t                      cover_count, meter_count, switch_count, cover_switch_count,
                                 endpoint_count;
    bool                         simultaneous_latching_pulses, momentary_switches, has_image_type;
} device_composition_t;

config_parse_result_t device_composition_validate(
    const device_composition_t *composition);

/**
 * Fingerprint of what persisted relay settings are bound to: the relay behind
 * each relay endpoint and the relay membership of each `K` group. Pins, relay
 * kind, dead times, cover pairs and tokens for buttons, LEDs and names do not
 * change it, so retuning or repinning a board keeps its relay settings.
 */
uint32_t device_composition_relay_topology(
    const device_composition_t *composition);

#endif
