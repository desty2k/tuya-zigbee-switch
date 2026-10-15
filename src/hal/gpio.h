#ifndef _HAL_GPIO_H_
#define _HAL_GPIO_H_

#include <stdint.h>

#define HAL_INVALID_PIN    0xFFFF

typedef uint16_t hal_gpio_pin_t;

/*
 * The level change happened while capture was blind (interrupts masked or the
 * device asleep) and was only observed at timestamp_ms. The previous level
 * may have ended at any point before that, so its duration is unknown.
 */
#define HAL_GPIO_EDGE_DEFERRED    0x01

typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        level;
    uint8_t        flags;        // HAL_GPIO_EDGE_* bits
    uint32_t       timestamp_ms; // time the new level was first observed
} hal_gpio_edge_t;

typedef void (*hal_gpio_edge_sink_t)(const hal_gpio_edge_t *edge);

typedef struct {
    uint32_t gpio_irq_count;
    uint32_t gpio_edges_captured;
    uint32_t gpio_rearm_limit_hits;
} hal_gpio_diagnostics_t;

typedef enum {
    HAL_GPIO_PULL_NONE    = 0,
    HAL_GPIO_PULL_UP      = 1,
    HAL_GPIO_PULL_UP_1M   = 2,
    HAL_GPIO_PULL_DOWN    = 3,
    HAL_GPIO_PULL_INVALID = 0xFF,
} hal_gpio_pull_t;

/**
 * Initialize GPIO pin as input/output with pull resistor configuration
 * @param gpio_pin GPIO pin identifier
 * @param is_input 1 for input, 0 for output
 * @param pull Pull resistor configuration
 */
void hal_gpio_init(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                   hal_gpio_pull_t pull);

/**
 * Hold every general-purpose pin that is not an output as an input with the
 * pull-down. A pin nothing configured otherwise stays in its reset state,
 * which floats on Telink and Silabs: a relay driver input behind it picks up
 * mains hum and half-energises the coil. Outputs keep their direction and
 * level. Pins the platform needs for itself (debug interface, flash, crystal,
 * debug UART) are left out; each platform lists its set.
 */
void hal_gpio_rest_all(void);

/**
 * Set output pin high
 * @param gpio_pin GPIO pin identifier
 */
void hal_gpio_set(hal_gpio_pin_t gpio_pin);

/**
 * Set output pin low
 * @param gpio_pin GPIO pin identifier
 */
void hal_gpio_clear(hal_gpio_pin_t gpio_pin);

/**
 * Set output pin based on value (0=low, non-zero=high)
 * @param gpio_pin GPIO pin identifier
 * @param value 0=low, non-zero=high
 */
static inline void hal_gpio_write(hal_gpio_pin_t gpio_pin, uint8_t value) {
    if (value) {
        hal_gpio_set(gpio_pin);
    } else {
        hal_gpio_clear(gpio_pin);
    }
}

/**
 * Read input pin state (0=low, 1=high)
 * @param gpio_pin GPIO pin identifier
 * @return Pin state (0=low, 1=high)
 */
uint8_t hal_gpio_read(hal_gpio_pin_t gpio_pin);

/** Register the single sink for captured GPIO transitions. */
void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink);

/** Begin capturing transitions on a pin without emitting its initial level. */
void hal_gpio_watch_pin(hal_gpio_pin_t gpio_pin);

/** Process GPIO IRQ work deferred to normal application context. */
void hal_gpio_process_pending(void);

/** Return a snapshot of platform GPIO capture counters. */
hal_gpio_diagnostics_t hal_gpio_get_diagnostics(void);

/**
 * Parse pin string ("A5", "B10") to pin identifier
 * @param s Pin string (e.g., "A5", "B10")
 * @return Pin identifier or HAL_INVALID_PIN
 */
hal_gpio_pin_t hal_gpio_parse_pin(const char *s);

/**
 * Parse pull resistor string ("u"/"d"/"f" for up/down/float)
 * @param pull_str Pull string ("u"/"d"/"f")
 * @return Pull configuration or HAL_GPIO_PULL_INVALID
 */
hal_gpio_pull_t hal_gpio_parse_pull(const char *pull_str);

#endif
