#ifndef BASE_COMPONENTS_GPIO_PROBE_SERVICE_H_
#define BASE_COMPONENTS_GPIO_PROBE_SERVICE_H_

#include "base_components/gpio_probe_codes.h"
#include "hal/gpio_probe.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * GPIO probe: finds the pins of a device that cannot be opened. Independent
 * of ZCL and NVM; nothing it learns or sets survives a reboot, and at boot it
 * touches no pin.
 *
 * Probeable pins are the platform's probe table minus every pin claimed by a
 * configured feature. Safety rules for output pulses:
 *  - only a pin classified FLOATING or DRIVEN_LOW by a pull test of this
 *    boot; a DRIVEN_LOW pin only to level 1 and for at most
 *    GPIO_PROBE_DRIVEN_LOW_PULSE_MAX_MS, since it may be a chip output;
 *  - only after an explicit arm of that pin, consumed by the pulse attempt
 *    and void after GPIO_PROBE_ARM_TIMEOUT_MS;
 *  - one pin at a time, never a pin under watch or pull test;
 *  - the pin must show its class again under both pulls right before it is
 *    driven;
 *  - 1 to GPIO_PROBE_PULSE_MAX_MS ms; the cutoff runs from a task and is
 *    re-checked by gpio_probe_service_poll(), both on the main loop, not in
 *    an interrupt. If the main loop stalls, the platform watchdog reset
 *    (Telink: 1 s) is the backstop, and the probe touches no pin at boot.
 *    A pulse whose task cannot be armed is refused before the pin is driven;
 *  - stop, cutoff and the end of every operation return the pins it touched
 *    to input without pull.
 */

#define GPIO_PROBE_WATCH_SLOTS    4

typedef struct {
    hal_gpio_probe_id_t pin;
    uint8_t             pull; // gpio_probe_pull_t
} gpio_probe_watch_pin_t;

typedef struct {
    hal_gpio_probe_id_t pin;
    uint32_t            edges;           // rising edges counted
    uint32_t            min_interval_us; // 0 until two edges were seen
    uint32_t            max_interval_us;
    uint8_t             level;           // level at the latest update
    uint8_t             flags;           // GPIO_PROBE_WATCH_* bits
} gpio_probe_watch_result_t;

/** Disable the probe and forget claims and results; touches no pin. */
void gpio_probe_service_init(void);

/** Keep a pin owned by a configured feature out of every probe operation. */
void gpio_probe_service_claim(uint8_t port, uint8_t index);

/** Accept commands once the device graph is built. */
void gpio_probe_service_enable(void);

/** Probeable pins, in the order every per-pin result uses; none while disabled. */
uint8_t gpio_probe_service_pin_count(void);
hal_gpio_probe_id_t gpio_probe_service_pin(uint8_t position);

gpio_probe_status_t gpio_probe_service_snapshot(uint32_t *levels);
gpio_probe_status_t gpio_probe_service_pull_test(const hal_gpio_probe_id_t *pins,
                                                 uint8_t count);
gpio_probe_class_t gpio_probe_service_class(uint8_t position);

gpio_probe_status_t gpio_probe_service_watch(const gpio_probe_watch_pin_t *pins,
                                             uint8_t count, uint16_t duration_s);
uint8_t gpio_probe_service_watch_count(void);
uint32_t gpio_probe_service_watch_elapsed_ms(void);
const gpio_probe_watch_result_t *gpio_probe_service_watch_result(uint8_t slot);

gpio_probe_status_t gpio_probe_service_arm(hal_gpio_probe_id_t pin);
gpio_probe_status_t gpio_probe_service_pulse(hal_gpio_probe_id_t pin,
                                             uint8_t level,
                                             uint16_t duration_ms);

/** End every operation and disarm; classifications are kept. */
void gpio_probe_service_stop(void);

/** GPIO_PROBE_STATE_* bits. */
uint8_t gpio_probe_service_state(void);

/** Main-loop work: pull test phases, watch counting and limits, pulse cutoff. */
void gpio_probe_service_poll(void);

#endif
