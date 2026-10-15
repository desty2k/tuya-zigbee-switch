#ifndef BASE_COMPONENTS_ENERGY_MEASUREMENT_BL0937_H_
#define BASE_COMPONENTS_ENERGY_MEASUREMENT_BL0937_H_

#include "base_components/energy_meter.h"
#include "hal/gpio.h"
#include "hal/gpio_counter.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * BL0937 pulse-output energy meter. CF carries active power; CF1 carries
 * RMS voltage or RMS current, selected by the SEL pin. Both outputs are
 * counted by hal/gpio_counter.h, never through the button edge pipeline.
 *
 * Sampling runs on each sample() call (the meter service period, 1 s):
 *
 * - CF is counted continuously. Energy is every CF pulse; power is the pulse
 *   rate over a window that closes once it holds BL0937_WINDOW_MIN_PULSES
 *   pulses or lasts BL0937_WINDOW_MAX_MS.
 * - CF1 alternates: SEL to voltage, one sample period discarded while CF1
 *   settles, one BL0937_VOLTAGE_WINDOW_MS window; SEL to current, one period
 *   discarded, then a window closed like the power window. A full cycle takes
 *   4 to 13 s at a 1 s sample period.
 */

/* CF1 voltage runs at ~2 kHz, so one second resolves 0.05 %. */
#define BL0937_VOLTAGE_WINDOW_MS    1000u
/* 50 pulses bound the counting error of a window to 2 %. */
#define BL0937_WINDOW_MIN_PULSES    50u
/* Longest window: a load drawing fewer than 5 pulses/s updates this often. */
#define BL0937_WINDOW_MAX_MS        10000u
/* Above any BL0937 full-scale output; a higher count is a counter fault. */
#define BL0937_MAX_PULSES_PER_MS    20u

typedef struct {
    hal_gpio_pin_t cf_pin;
    hal_gpio_pin_t cf1_pin;
    hal_gpio_pin_t sel_pin;
    /* Board wiring: true when SEL high makes CF1 carry voltage. */
    bool           sel_high_selects_voltage;

    /* One pulse per second equals this many uV, uA and uW (board divider
     * and shunt); the meter service applies calibration gains on top. */
    uint32_t       voltage_uv_per_hz;
    uint32_t       current_ua_per_hz;
    uint32_t       power_uw_per_hz;
} bl0937_config_t;

typedef enum {
    BL0937_PHASE_VOLTAGE_SETTLE,
    BL0937_PHASE_VOLTAGE,
    BL0937_PHASE_CURRENT_SETTLE,
    BL0937_PHASE_CURRENT,
} bl0937_phase_t;

typedef struct {
    bl0937_config_t       config;
    hal_gpio_counter_id_t cf_counter;
    hal_gpio_counter_id_t cf1_counter;
    bl0937_phase_t        phase;
    uint32_t              cf_last_total;
    uint32_t              cf_last_ms;
    /* uW*s not yet converted to whole uWh (always < 3600). */
    uint32_t              energy_remainder;
    uint32_t              power_window_total;
    uint32_t              power_window_ms;
    uint32_t              cf1_window_total;
    uint32_t              cf1_window_ms;
    uint32_t              voltage_mv;
    uint32_t              current_ma;
    uint32_t              power_mw;
    uint16_t              valid_mask;
    /* Windows and energy deltas discarded as implausible. */
    uint32_t              error_count;
} bl0937_t;

/* Opens both counters and drives SEL; false leaves no counter open. */
bool bl0937_init(bl0937_t *dev, const bl0937_config_t *config);
void bl0937_close(bl0937_t *dev);
meter_driver_t bl0937_as_meter(bl0937_t *dev);

#endif /* BASE_COMPONENTS_ENERGY_MEASUREMENT_BL0937_H_ */
