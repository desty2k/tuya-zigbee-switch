#ifndef _HAL_GPIO_H_
#define _HAL_GPIO_H_

#include <stdint.h>

#define HAL_INVALID_PIN    0xFFFF

typedef uint16_t hal_gpio_pin_t;

typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        level;
    uint32_t       timestamp_ms;
    uint32_t       seq;
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

/** Stop capturing transitions on a pin. */
void hal_gpio_unwatch_pin(hal_gpio_pin_t gpio_pin);

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
