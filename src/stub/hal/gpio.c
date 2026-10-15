#include "hal/gpio.h"
#include "hal/gpio_pin_text.h"
#include "hal/timer.h"
#include "stub/machine_io.h"
#include "stub/hal/gpio_counter.h"
#if FEATURE_PROBE
#include "stub/hal/gpio_probe.h"
#endif
#include "stub/hal/stub.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GPIO_PINS    STUB_GPIO_PIN_LIMIT
#define MAX_CALLBACKS    32

typedef struct {
    uint8_t         initialized;
    uint8_t         is_input;
    uint8_t         value;
    hal_gpio_pull_t pull;
    uint8_t         watched;
    uint8_t         last_level;
} stub_gpio_pin_t;

static stub_gpio_pin_t        gpio_pins[MAX_GPIO_PINS];
static hal_gpio_edge_sink_t   edge_sink;
static hal_gpio_diagnostics_t diagnostics;
static uint32_t init_count;
static uint32_t write_count;

void ensure_valid_input_pin(hal_gpio_pin_t gpio_pin);
void ensure_valid_output_pin(hal_gpio_pin_t gpio_pin);

void hal_gpio_init(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                   hal_gpio_pull_t pull) {
    if (gpio_pin >= MAX_GPIO_PINS)
        return;

    gpio_pins[gpio_pin].initialized = 1;
    init_count++;
    gpio_pins[gpio_pin].is_input   = is_input;
    gpio_pins[gpio_pin].pull       = pull;
    gpio_pins[gpio_pin].value      = (pull == HAL_GPIO_PULL_UP) ? 1 : 0;
    gpio_pins[gpio_pin].watched    = 0;
    gpio_pins[gpio_pin].last_level = gpio_pins[gpio_pin].value;

    io_log("GPIO", "Init pin %d as %s, pull=%d", gpio_pin,
           is_input ? "input" : "output", pull);
}

/*
 * The rest set of the TLSR8258 (src/telink/hal/gpio.c) in its pin encoding:
 * ports A-D, PA7 (SWS) left out. Probe images rest the probe model's pins
 * too: on the device both models are the same pads.
 */
void hal_gpio_rest_all(void) {
    for (uint8_t port = 0; port < 4; port++) {
        for (uint8_t index = 0; index < 8; index++) {
            hal_gpio_pin_t   pin   = (hal_gpio_pin_t)(((uint16_t)port << 8) | (1U << index));
            stub_gpio_pin_t *state = &gpio_pins[pin];

            if ((port == 0 && index == 7) ||
                (state->initialized && !state->is_input)) {
                continue;
            }
            if (!state->initialized) {
                state->last_level = 0;
            }
            state->initialized = 1;
            state->value       = 0;
            state->is_input    = 1;
            state->pull        = HAL_GPIO_PULL_DOWN;
#if FEATURE_PROBE
            hal_gpio_probe_input(HAL_GPIO_PROBE_ID(port, index), HAL_GPIO_PULL_DOWN);
#endif
            io_log("GPIO", "Rest pin %d on the pull-down", pin);
        }
    }
}

bool stub_gpio_pin_state(hal_gpio_pin_t gpio_pin, stub_gpio_pin_state_t *out) {
    if (gpio_pin >= MAX_GPIO_PINS) {
        return false;
    }
    out->initialized = gpio_pins[gpio_pin].initialized;
    out->is_input    = gpio_pins[gpio_pin].is_input;
    out->pull        = gpio_pins[gpio_pin].pull;
    out->value       = gpio_pins[gpio_pin].value;
    return true;
}

void hal_gpio_set(hal_gpio_pin_t gpio_pin) {
    ensure_valid_output_pin(gpio_pin);
    stub_gpio_counter_advance();

    gpio_pins[gpio_pin].value = 1;
    write_count++;
    io_log("GPIO", "Set pin %d = 1", gpio_pin);
    io_evt("gpio pin=%d value=%d", gpio_pin, 1);
}

void hal_gpio_clear(hal_gpio_pin_t gpio_pin) {
    ensure_valid_output_pin(gpio_pin);
    stub_gpio_counter_advance();

    gpio_pins[gpio_pin].value = 0;
    write_count++;
    io_log("GPIO", "Clear pin %d = 0", gpio_pin);
    io_evt("gpio pin=%d value=%d", gpio_pin, 0);
}

uint8_t hal_gpio_read(hal_gpio_pin_t gpio_pin) {
    ensure_valid_input_pin(gpio_pin);

    io_log("GPIO", "Read pin %d = %d", gpio_pin, gpio_pins[gpio_pin].value);
    return gpio_pins[gpio_pin].value;
}

void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink) {
    edge_sink = sink;
}

void hal_gpio_watch_pin(hal_gpio_pin_t gpio_pin) {
    ensure_valid_input_pin(gpio_pin);

    gpio_pins[gpio_pin].last_level = gpio_pins[gpio_pin].value;
    gpio_pins[gpio_pin].watched    = 1;
    io_log("GPIO", "Watching pin %d", gpio_pin);
}

void hal_gpio_process_pending(void) {
}

hal_gpio_diagnostics_t hal_gpio_get_diagnostics(void) {
    return diagnostics;
}

/*
 * Telink's encoding (port << 8 | 1 << index, index 0..7) over ports A..Z. A
 * pin's id differs from any plain port/index packing of the same pin, so code
 * that derives pin ids without this function drives the wrong pins in tests.
 */
hal_gpio_pin_t hal_gpio_parse_pin(const char *s) {
    uint8_t port;
    uint8_t index;

    if (!hal_gpio_pin_text_split(s, &port, &index) || index > 7) {
        io_log("GPIO", "Error: Invalid GPIO pin: '%s'", s ? s : "(null)");
        return HAL_INVALID_PIN;
    }

    hal_gpio_pin_t res = (hal_gpio_pin_t)(((uint16_t)port << 8) | (1U << index));
    io_log("GPIO", "Parsed GPIO pin '%s' as %d", s, res);
    return res;
}

hal_gpio_pull_t hal_gpio_parse_pull(const char *pull_str) {
    if (!pull_str) {
        io_log("GPIO", "Error: NULL string passed to hal_parse_gpio_pull");
        return HAL_GPIO_PULL_INVALID;
    }

    if (strcmp(pull_str, "") == 0)
        return HAL_GPIO_PULL_NONE;

    if (strcmp(pull_str, "u") == 0)
        return HAL_GPIO_PULL_UP;

    if (strcmp(pull_str, "d") == 0)
        return HAL_GPIO_PULL_DOWN;

    io_log("GPIO", "Error: Invalid GPIO pull string: '%s'", pull_str);
    return HAL_GPIO_PULL_INVALID;
}

// Stub-specific functions for testing

static void stub_gpio_capture(hal_gpio_pin_t gpio_pin, uint8_t flags) {
    uint8_t value = gpio_pins[gpio_pin].value;

    if (value == gpio_pins[gpio_pin].last_level) {
        return;
    }

    hal_gpio_edge_t edge = {
        .pin          = gpio_pin,
        .level        = value,
        .flags        = flags,
        .timestamp_ms = hal_millis(),
    };

    gpio_pins[gpio_pin].last_level = value;
    diagnostics.gpio_edges_captured++;
    if (edge_sink != NULL) {
        edge_sink(&edge);
    }
}

void stub_gpio_simulate_input(hal_gpio_pin_t gpio_pin, uint8_t value) {
    if (gpio_pin >= MAX_GPIO_PINS) {
        io_log("GPIO", "Error: Invalid input pin %d for simulation", gpio_pin);
        return;
    }

    uint8_t old_value = gpio_pins[gpio_pin].value;
    gpio_pins[gpio_pin].value = value;

    if (old_value != value) {
        stub_gpio_counter_capture_edge(gpio_pin, old_value, value, hal_millis());
    }

    if (old_value != value && gpio_pins[gpio_pin].watched) {
        diagnostics.gpio_irq_count++;
        stub_gpio_capture(gpio_pin, 0);
    }

    io_log("GPIO", "Simulated input pin %d = %d", gpio_pin, value);
}

void stub_gpio_burst(hal_gpio_pin_t gpio_pin, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        stub_gpio_simulate_input(gpio_pin, gpio_pins[gpio_pin].value ? 0 : 1);
    }
}

/*
 * Model Telink's deferred capture: the level changes while watched-pin
 * interrupts are disabled, the main loop stalls for lag_ms, and the change is
 * only observed (and stamped) when hal_gpio_process_pending() runs.
 */
void stub_gpio_simulate_late_input(hal_gpio_pin_t gpio_pin, uint8_t value,
                                   uint32_t lag_ms) {
    if (gpio_pin >= MAX_GPIO_PINS) {
        io_log("GPIO", "Error: Invalid input pin %d for simulation", gpio_pin);
        return;
    }
    if (gpio_pins[gpio_pin].value != value) {
        stub_gpio_counter_capture_edge(gpio_pin, gpio_pins[gpio_pin].value,
                                       value, hal_millis());
    }
    gpio_pins[gpio_pin].value = value;
    stub_millis_step(lag_ms);
    if (gpio_pins[gpio_pin].watched) {
        stub_gpio_capture(gpio_pin, HAL_GPIO_EDGE_DEFERRED);
    }
    io_log("GPIO", "Simulated late input pin %d = %d (lag %u ms)", gpio_pin,
           value, lag_ms);
}

uint8_t stub_gpio_get_output(hal_gpio_pin_t gpio_pin) {
    if (gpio_pin >= MAX_GPIO_PINS) {
        io_log("GPIO", "Error: Invalid GPIO pin %d for output", gpio_pin);
        return 0;
    }
    return gpio_pins[gpio_pin].value;
}

uint32_t stub_gpio_init_count(void) {
    return init_count;
}

uint32_t stub_gpio_write_count(void) {
    return write_count;
}

// Helper funcs

void ensure_valid_pin(hal_gpio_pin_t gpio_pin) {
    if (gpio_pin >= MAX_GPIO_PINS) {
        io_log("GPIO", "Error: GPIO pin %d out of range", gpio_pin);
        exit(1);
    }
    if (!gpio_pins[gpio_pin].initialized) {
        io_log("GPIO", "Error: GPIO pin %d not initialized", gpio_pin);
        exit(1);
    }
}

void ensure_valid_input_pin(hal_gpio_pin_t gpio_pin) {
    ensure_valid_pin(gpio_pin);
    if (!gpio_pins[gpio_pin].is_input) {
        io_log("GPIO", "Error: Attempt to use output GPIO pin %d as input",
               gpio_pin);
        exit(1);
    }
}

void ensure_valid_output_pin(hal_gpio_pin_t gpio_pin) {
    ensure_valid_pin(gpio_pin);
    if (gpio_pins[gpio_pin].is_input) {
        io_log("GPIO", "Error: Attempt to use input GPIO pin %d as output",
               gpio_pin);
        exit(1);
    }
}
