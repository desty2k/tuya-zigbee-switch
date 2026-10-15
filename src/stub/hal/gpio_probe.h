#ifndef STUB_HAL_GPIO_PROBE_H_
#define STUB_HAL_GPIO_PROBE_H_

#include "hal/gpio_probe.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Electrical model of the pins behind hal/gpio_probe.h. Every pin starts as
 * a floating node with the MCU output off. Sources:
 *  - floating: follows the MCU pull or drive, otherwise keeps its last level;
 *  - driven: an external circuit holds `level` whatever the MCU does; MCU
 *    drive against it is counted as contention;
 *  - train: an external square wave, rising at origin + k * period_us and
 *    high for high_us of each period; MCU drive against it is contention;
 *  - base: the base of an NPN relay driver behind a series resistor of
 *    base_ohm. Base current flows when the pin is driven high or pulled up
 *    (TLSR8258 nominal pulls: up 10 kOhm or 1 MOhm, down 100 kOhm, 3.3 V,
 *    V_BE 0.6 V, input high from 0.7 VDD); the relay is energised while it is
 *    at least pull_in_ua.
 * Time comes from hal_millis(), at microsecond resolution for edges.
 */
typedef enum {
    STUB_PROBE_SOURCE_FLOATING,
    STUB_PROBE_SOURCE_DRIVEN,
    STUB_PROBE_SOURCE_TRAIN,
    STUB_PROBE_SOURCE_BASE,
} stub_probe_source_t;

typedef struct {
    stub_probe_source_t source;
    uint8_t             level;          // current resolved level
    uint8_t             output_enabled; // MCU output driver on
    uint8_t             output_level;
    hal_gpio_pull_t     pull;
    uint32_t            drives;         // hal_gpio_probe_drive() calls
    uint32_t            contention;     // drives against an external source
    uint8_t             relay_on;       // base: relay energised now
    uint32_t            relay_pull_ins; // base: times the relay was energised
} stub_probe_pin_info_t;

void stub_gpio_probe_reset(void);
bool stub_gpio_probe_set_floating(hal_gpio_probe_id_t id);
bool stub_gpio_probe_set_driven(hal_gpio_probe_id_t id, uint8_t level);
bool stub_gpio_probe_set_train(hal_gpio_probe_id_t id, uint32_t period_us,
                               uint32_t high_us);
bool stub_gpio_probe_set_base(hal_gpio_probe_id_t id, uint32_t base_ohm,
                              uint32_t pull_in_ua);
bool stub_gpio_probe_info(hal_gpio_probe_id_t id, stub_probe_pin_info_t *out);

/** Portable id of a pin name such as "C3"; false if not in the probe table. */
bool stub_gpio_probe_parse(const char *name, hal_gpio_probe_id_t *id);

#endif
