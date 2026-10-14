#ifndef HAL_GPIO_PROBE_H_
#define HAL_GPIO_PROBE_H_

#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Raw pin access for the GPIO probe (base_components/gpio_probe_service.c),
 * built only into images with the `probe` feature.
 *
 * Pins are named by portable ids, port << 8 | index with port A = 0, which the
 * platform translates to its own encoding. Valid ids are exactly those of the
 * platform's probe table; the table leaves out every pin the platform itself
 * needs (debug interface, flash, crystal, debug UART). Functions other than
 * hal_gpio_probe_pins() expect an id from that table.
 */
typedef uint16_t hal_gpio_probe_id_t;

#define HAL_GPIO_PROBE_ID(port, index) \
        ((hal_gpio_probe_id_t)(((uint16_t)(port) << 8) | (uint8_t)(index)))
#define HAL_GPIO_PROBE_PORT(id)     ((uint8_t)((id) >> 8))
#define HAL_GPIO_PROBE_INDEX(id)    ((uint8_t)((id) & 0xFFu))

/* Upper bound of every platform's probe table. */
#define HAL_GPIO_PROBE_MAX_PINS       32

/*
 * Rising edges a counter accepts between two hal_gpio_probe_counter_read()
 * calls before it disables its own interrupt and reports `saturated`. Bounds
 * the interrupt load of a fast signal when the main loop is late: 2000 edges
 * cover 100 ms at the service's 20 kHz rate ceiling.
 */
#define HAL_GPIO_PROBE_BURST_LIMIT    2000u

/** Probeable pins of this MCU; returns the count and points *ids at them. */
uint8_t hal_gpio_probe_pins(const hal_gpio_probe_id_t **ids);

/** Input level of a pin, without changing its direction or pull. */
uint8_t hal_gpio_probe_read(hal_gpio_probe_id_t id);

/**
 * Make a pin an input with the given pull and its output driver off. Returns
 * once the level has settled under the new pull (Telink: 100 kOhm pull-down
 * against the pin capacitance, a few microseconds).
 */
void hal_gpio_probe_input(hal_gpio_probe_id_t id, hal_gpio_pull_t pull);

/** Drive a pin as a push-pull output at `level` (0 or 1). */
void hal_gpio_probe_drive(hal_gpio_probe_id_t id, uint8_t level);

typedef struct {
    uint32_t edges;          // rising edges since start, wraps at 2^32
    uint32_t last_edge_tick; // hal_gpio_probe_ticks() at the latest edge
    uint8_t  saturated;      // stopped by HAL_GPIO_PROBE_BURST_LIMIT
} hal_gpio_probe_count_t;

/** Number of pins that can count edges at the same time. */
uint8_t hal_gpio_probe_counter_capacity(void);

/**
 * Count rising edges of a pin on counter `slot`. The pin's direction and pull
 * are left as they are. The interrupt handler only increments the count and
 * stores the tick; it stops itself at HAL_GPIO_PROBE_BURST_LIMIT.
 */
bool hal_gpio_probe_counter_start(uint8_t slot, hal_gpio_probe_id_t id);
void hal_gpio_probe_counter_stop(uint8_t slot);

/** Copy a counter atomically and renew its burst budget. */
void hal_gpio_probe_counter_read(uint8_t slot, hal_gpio_probe_count_t *out);

/** Free-running 32-bit tick used for edge timestamps. */
uint32_t hal_gpio_probe_ticks(void);
uint32_t hal_gpio_probe_ticks_per_us(void);

#endif
