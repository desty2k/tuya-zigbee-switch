#include "bl0937.h"

#include "base_components/fixed_math.h"
#include "hal/timer.h"
#include <string.h>

/* uW*s per uWh. */
#define BL0937_SECONDS_PER_HOUR    3600u

static bool pulses_plausible(uint32_t pulses, uint32_t elapsed_ms) {
    return pulses <= (elapsed_ms + 1u) * BL0937_MAX_PULSES_PER_MS;
}

/* pulses / elapsed_s * scale_per_hz, in milli-units of the scale. */
static uint32_t rate_to_milli(uint32_t pulses, uint32_t scale_per_hz,
                              uint32_t elapsed_ms) {
    return fixed_muldiv_u32(pulses, scale_per_hz, elapsed_ms);
}

static void select_voltage(bl0937_t *dev, bool voltage) {
    hal_gpio_write(dev->config.sel_pin,
                   voltage == dev->config.sel_high_selects_voltage ? 1 : 0);
}

static uint64_t energy_from_pulses(bl0937_t *dev, uint32_t pulses) {
    uint64_t microjoules = fixed_mul_u32(pulses, dev->config.power_uw_per_hz) +
                           dev->energy_remainder;

    return fixed_div_u64(microjoules, BL0937_SECONDS_PER_HOUR,
                         &dev->energy_remainder);
}

static void update_power(bl0937_t *dev, uint32_t cf_total, uint32_t now) {
    uint32_t pulses  = cf_total - dev->power_window_total;
    uint32_t elapsed = now - dev->power_window_ms;

    if (elapsed == 0 ||
        (pulses < BL0937_WINDOW_MIN_PULSES && elapsed < BL0937_WINDOW_MAX_MS)) {
        return;
    }
    if (pulses_plausible(pulses, elapsed)) {
        dev->power_mw    = rate_to_milli(pulses, dev->config.power_uw_per_hz, elapsed);
        dev->valid_mask |= METER_SAMPLE_ACTIVE_POWER;
    } else {
        dev->error_count++;
    }
    dev->power_window_total = cf_total;
    dev->power_window_ms    = now;
}

static void restart_cf1_window(bl0937_t *dev, uint32_t cf1_total, uint32_t now) {
    dev->cf1_window_total = cf1_total;
    dev->cf1_window_ms    = now;
}

static void update_cf1(bl0937_t *dev, uint32_t cf1_total, uint32_t now) {
    uint32_t pulses  = cf1_total - dev->cf1_window_total;
    uint32_t elapsed = now - dev->cf1_window_ms;

    switch (dev->phase) {
    case BL0937_PHASE_VOLTAGE_SETTLE:
        restart_cf1_window(dev, cf1_total, now);
        dev->phase = BL0937_PHASE_VOLTAGE;
        break;

    case BL0937_PHASE_VOLTAGE:
        if (elapsed < BL0937_VOLTAGE_WINDOW_MS) {
            break;
        }
        if (pulses_plausible(pulses, elapsed)) {
            dev->voltage_mv  = rate_to_milli(pulses, dev->config.voltage_uv_per_hz, elapsed);
            dev->valid_mask |= METER_SAMPLE_VOLTAGE;
        } else {
            dev->error_count++;
        }
        select_voltage(dev, false);
        dev->phase = BL0937_PHASE_CURRENT_SETTLE;
        break;

    case BL0937_PHASE_CURRENT_SETTLE:
        restart_cf1_window(dev, cf1_total, now);
        dev->phase = BL0937_PHASE_CURRENT;
        break;

    case BL0937_PHASE_CURRENT:
        if (elapsed == 0 ||
            (pulses < BL0937_WINDOW_MIN_PULSES && elapsed < BL0937_WINDOW_MAX_MS)) {
            break;
        }
        if (pulses_plausible(pulses, elapsed)) {
            dev->current_ma  = rate_to_milli(pulses, dev->config.current_ua_per_hz, elapsed);
            dev->valid_mask |= METER_SAMPLE_CURRENT;
        } else {
            dev->error_count++;
        }
        select_voltage(dev, true);
        dev->phase = BL0937_PHASE_VOLTAGE_SETTLE;
        break;
    }
}

static bool bl0937_sample(void *context, meter_sample_t *out) {
    bl0937_t *dev = context;
    hal_gpio_counter_snapshot_t cf;
    hal_gpio_counter_snapshot_t cf1;
    uint32_t now;
    uint32_t cf_pulses;
    uint64_t energy_uwh = 0;

    if (dev == NULL || out == NULL ||
        !hal_gpio_counter_snapshot(dev->cf_counter, &cf) ||
        !hal_gpio_counter_snapshot(dev->cf1_counter, &cf1)) {
        return false;
    }
    now       = hal_millis();
    cf_pulses = cf.total - dev->cf_last_total;
    if (pulses_plausible(cf_pulses, now - dev->cf_last_ms)) {
        energy_uwh = energy_from_pulses(dev, cf_pulses);
    } else {
        dev->error_count++;
    }
    dev->cf_last_total = cf.total;
    dev->cf_last_ms    = now;
    update_power(dev, cf.total, now);
    update_cf1(dev, cf1.total, now);
    *out = (meter_sample_t){
        .voltage_mv       = dev->voltage_mv,
        .current_ma       = dev->current_ma,
        .active_power_mw  = dev->power_mw > INT32_MAX ? INT32_MAX : (int32_t)dev->power_mw,
        .energy_delta_uwh = energy_uwh,
        .valid_mask       = (uint16_t)(dev->valid_mask | METER_SAMPLE_ENERGY),
    };
    return true;
}

static void bl0937_reset_energy_remainder(void *context) {
    bl0937_t *dev = context;

    if (dev != NULL) {
        dev->energy_remainder = 0;
    }
}

static const meter_driver_ops_t bl0937_ops = {
    .sample                 = bl0937_sample,
    .reset_energy_remainder = bl0937_reset_energy_remainder,
};

bool bl0937_init(bl0937_t *dev, const bl0937_config_t *config) {
    hal_gpio_counter_snapshot_t cf;
    hal_gpio_counter_snapshot_t cf1;

    if (dev == NULL || config == NULL || config->voltage_uv_per_hz == 0 ||
        config->current_ua_per_hz == 0 || config->power_uw_per_hz == 0) {
        return false;
    }
    memset(dev, 0, sizeof(*dev));
    dev->config     = *config;
    dev->cf_counter = hal_gpio_counter_open(config->cf_pin, HAL_GPIO_COUNTER_EDGE_RISING,
                                            HAL_GPIO_PULL_NONE);
    dev->cf1_counter = hal_gpio_counter_open(config->cf1_pin, HAL_GPIO_COUNTER_EDGE_RISING,
                                             HAL_GPIO_PULL_NONE);
    if (!hal_gpio_counter_snapshot(dev->cf_counter, &cf) ||
        !hal_gpio_counter_snapshot(dev->cf1_counter, &cf1)) {
        bl0937_close(dev);
        return false;
    }
    hal_gpio_init(config->sel_pin, 0, HAL_GPIO_PULL_NONE);
    select_voltage(dev, true);
    dev->phase              = BL0937_PHASE_VOLTAGE_SETTLE;
    dev->cf_last_total      = cf.total;
    dev->cf_last_ms         = hal_millis();
    dev->power_window_total = cf.total;
    dev->power_window_ms    = dev->cf_last_ms;
    restart_cf1_window(dev, cf1.total, dev->cf_last_ms);
    return true;
}

void bl0937_close(bl0937_t *dev) {
    if (dev == NULL) {
        return;
    }
    hal_gpio_counter_close(dev->cf_counter);
    hal_gpio_counter_close(dev->cf1_counter);
    dev->cf_counter  = HAL_GPIO_COUNTER_INVALID_ID;
    dev->cf1_counter = HAL_GPIO_COUNTER_INVALID_ID;
}

meter_driver_t bl0937_as_meter(bl0937_t *dev) {
    return (meter_driver_t){
               .ops     = &bl0937_ops,
               .context = dev,
    };
}
