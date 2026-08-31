#ifndef _BUTTON_INPUT_H_
#define _BUTTON_INPUT_H_

#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#define BUTTON_INPUT_MAX              16
#define BUTTON_DEBOUNCE_DEFAULT_MS    8

typedef enum {
    BUTTON_STATE_UP   = 0,
    BUTTON_STATE_DOWN = 1,
} button_state_t;

typedef enum {
    BUTTON_EVENT_DOWN,
    BUTTON_EVENT_UP,
} button_event_type_t;

typedef struct {
    uint8_t             button_id;
    button_event_type_t type;
    uint32_t            timestamp_ms;
    uint32_t            seq;
    uint32_t            press_id;
} button_event_t;

typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        active_high;
    uint8_t        electrical_active_high;
    uint16_t       debounce_ms;
} button_config_t;

typedef struct {
    button_state_t stable_state;
    button_state_t raw_state;
    uint32_t       raw_since_ms;
    uint32_t       press_id;
    bool           boot_press;
} button_runtime_t;

void button_input_init(void);

/** Schedule input processing requested by a captured GPIO edge. */
void button_input_process_pending(void);
uint8_t button_input_add(const button_config_t *config);
void button_input_set_active_high(uint8_t button_id, bool active_high);
void button_input_apply_switch_mode(uint8_t button_id, bool momentary_nc);
void button_input_set_debounce_ms(uint8_t button_id, uint16_t debounce_ms);
bool button_input_is_down(uint8_t button_id);
bool button_input_boot_press(uint8_t button_id);
uint8_t button_input_count(void);
uint32_t button_input_gpio_edges_dropped(void);
uint32_t button_input_events_emitted(void);

#endif
