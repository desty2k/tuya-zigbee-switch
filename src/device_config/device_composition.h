#ifndef DEVICE_CONFIG_DEVICE_COMPOSITION_H_
#define DEVICE_CONFIG_DEVICE_COMPOSITION_H_

#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#define DEVICE_COMPOSITION_MAX_RELAYS            10
#define DEVICE_COMPOSITION_MAX_BUTTONS           11
#define DEVICE_COMPOSITION_MAX_INDICATORS        5
#define DEVICE_COMPOSITION_MAX_COVERS            3
#define DEVICE_COMPOSITION_MAX_SWITCHES          10
#define DEVICE_COMPOSITION_MAX_COVER_SWITCHES    3
#define DEVICE_COMPOSITION_MAX_INTERLOCKS        10
#define DEVICE_COMPOSITION_MAX_ENDPOINTS         10
#define DEVICE_COMPOSITION_MAX_CLUSTERS          48

typedef enum { CONFIG_PARSE_OK, CONFIG_PARSE_SYNTAX, CONFIG_PARSE_LIMIT,
               CONFIG_PARSE_REFERENCE, CONFIG_PARSE_PIN_CONFLICT,
               CONFIG_PARSE_RESOURCE } config_parse_result_t;
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
typedef struct { uint8_t button_id; uint8_t relay_id; }                device_switch_config_t;
typedef struct { uint8_t open_button_id; uint8_t close_button_id; }    device_cover_switch_config_t;
typedef struct {
    char                         manufacturer[32]; char model[32];
    device_button_config_t       buttons[DEVICE_COMPOSITION_MAX_BUTTONS];
    device_relay_config_t        relays[DEVICE_COMPOSITION_MAX_RELAYS];
    device_indicator_config_t    indicators[DEVICE_COMPOSITION_MAX_INDICATORS];
    device_interlock_config_t    interlocks[DEVICE_COMPOSITION_MAX_INTERLOCKS];
    device_cover_config_t        covers[DEVICE_COMPOSITION_MAX_COVERS];
    device_switch_config_t       switches[DEVICE_COMPOSITION_MAX_SWITCHES];
    device_cover_switch_config_t cover_switches[DEVICE_COMPOSITION_MAX_COVER_SWITCHES];
    uint8_t                      relay_endpoint_ids[DEVICE_COMPOSITION_MAX_RELAYS];
    hal_gpio_pin_t               battery_pin;
    uint8_t                      relay_count, relay_endpoint_count, button_count, indicator_count,
                                 interlock_count;
    uint8_t                      cover_count, switch_count, cover_switch_count, endpoint_count;
    bool                         simultaneous_latching_pulses, momentary_switches;
} device_composition_t;
#endif
