#include "fake_gpio.h"
#include "fake_clock.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_GPIO_PIN_MAX    256

typedef struct {
    uint8_t level;
    bool    watched;
} fake_gpio_pin_t;

static fake_gpio_pin_t       fake_gpio_pins[FAKE_GPIO_PIN_MAX];
static hal_gpio_edge_sink_t  fake_gpio_sink;
static hal_gpio_diagnostics_t fake_gpio_diagnostics;
static uint32_t fake_gpio_seq;

void fake_gpio_reset(void) {
    memset(fake_gpio_pins, 0, sizeof(fake_gpio_pins));
    memset(&fake_gpio_diagnostics, 0, sizeof(fake_gpio_diagnostics));
    fake_gpio_sink = NULL;
    fake_gpio_seq  = 0;
}

void fake_gpio_set_input(hal_gpio_pin_t pin, uint8_t level) {
    if (pin < FAKE_GPIO_PIN_MAX) {
        fake_gpio_pins[pin].level = level != 0;
    }
}

void fake_gpio_inject_edge(hal_gpio_pin_t pin, uint8_t level,
                           uint32_t timestamp_ms) {
    if (pin >= FAKE_GPIO_PIN_MAX || !fake_gpio_pins[pin].watched ||
        fake_gpio_pins[pin].level == (level != 0)) {
        return;
    }

    fake_clock_set(timestamp_ms);
    fake_gpio_pins[pin].level = level != 0;
    fake_gpio_diagnostics.gpio_irq_count++;
    fake_gpio_diagnostics.gpio_edges_captured++;
    if (fake_gpio_sink != NULL) {
        hal_gpio_edge_t edge = {
            .pin          = pin,
            .level        = level != 0,
            .timestamp_ms = timestamp_ms,
            .seq          = fake_gpio_seq++,
        };
        fake_gpio_sink(&edge);
    }
}

void hal_gpio_init(hal_gpio_pin_t pin, uint8_t is_input,
                   hal_gpio_pull_t pull) {
    (void)is_input;
    fake_gpio_set_input(pin, pull == HAL_GPIO_PULL_UP ||
                        pull == HAL_GPIO_PULL_UP_1M);
}

void hal_gpio_set(hal_gpio_pin_t pin) {
    fake_gpio_set_input(pin, 1);
}

void hal_gpio_clear(hal_gpio_pin_t pin) {
    fake_gpio_set_input(pin, 0);
}

uint8_t hal_gpio_read(hal_gpio_pin_t pin) {
    return pin < FAKE_GPIO_PIN_MAX ? fake_gpio_pins[pin].level : 0;
}

void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink) {
    fake_gpio_sink = sink;
}

void hal_gpio_watch_pin(hal_gpio_pin_t pin) {
    if (pin < FAKE_GPIO_PIN_MAX) {
        fake_gpio_pins[pin].watched = true;
    }
}

void hal_gpio_unwatch_pin(hal_gpio_pin_t pin) {
    if (pin < FAKE_GPIO_PIN_MAX) {
        fake_gpio_pins[pin].watched = false;
    }
}

void hal_gpio_process_pending(void) {
}

hal_gpio_diagnostics_t hal_gpio_get_diagnostics(void) {
    return fake_gpio_diagnostics;
}

hal_gpio_pin_t hal_gpio_parse_pin(const char *text) {
    if (text == NULL || text[0] < 'A' || text[0] > 'Z') {
        return HAL_INVALID_PIN;
    }
    return (hal_gpio_pin_t)(((text[0] - 'A') << 4) | atoi(&text[1]));
}

hal_gpio_pull_t hal_gpio_parse_pull(const char *text) {
    if (text == NULL) {
        return HAL_GPIO_PULL_INVALID;
    }
    if (strcmp(text, "u") == 0) {
        return HAL_GPIO_PULL_UP;
    }
    if (strcmp(text, "d") == 0) {
        return HAL_GPIO_PULL_DOWN;
    }
    if (strcmp(text, "f") == 0 || text[0] == '\0') {
        return HAL_GPIO_PULL_NONE;
    }
    return HAL_GPIO_PULL_INVALID;
}
