#ifndef ZIGBEE_METER_UNITS_H_
#define ZIGBEE_METER_UNITS_H_

#include "base_components/fixed_math.h"
#include <stdint.h>

/*
 * Wire units of the measurement clusters, set by their multiplier and divisor
 * attributes:
 *
 * - RMSVoltage: uint16 in 0.1 V (multiplier 1, divisor 10);
 * - RMSCurrent: uint16 in mA (multiplier 1, divisor 1000);
 * - ActivePower: int16 in W (multiplier 1, divisor 1). 0.1 W would end at
 *   3276.7 W, below the 16 A / 230 V (3680 W) class of the outlets;
 * - CurrentSummationDelivered: uint48 in Wh (multiplier 1, divisor 1000 kWh).
 *
 * Values round to nearest and saturate below the ZCL invalid value, which
 * stays reserved for "no valid measurement yet".
 */
#define METER_UNITS_VOLTAGE_DIVISOR    10u
#define METER_UNITS_CURRENT_DIVISOR    1000u
#define METER_UNITS_POWER_DIVISOR      1u
#define METER_UNITS_ENERGY_DIVISOR     1000u

#define METER_UNITS_INVALID_U16        0xFFFFu
#define METER_UNITS_INVALID_S16        ((int16_t)-32768)
#define METER_UNITS_MAX_U48            0xFFFFFFFFFFFFull

static inline uint16_t meter_units_saturate_u16(uint32_t value) {
    return value >= METER_UNITS_INVALID_U16 ? METER_UNITS_INVALID_U16 - 1u : (uint16_t)value;
}

static inline uint16_t meter_units_voltage_dv(uint32_t voltage_mv) {
    return meter_units_saturate_u16(voltage_mv / 100u + (voltage_mv % 100u >= 50u ? 1u : 0u));
}

static inline uint16_t meter_units_current_ma(uint32_t current_ma) {
    return meter_units_saturate_u16(current_ma);
}

static inline int16_t meter_units_power_w(int32_t power_mw) {
    if (power_mw >= (int32_t)INT16_MAX * 1000) {
        return INT16_MAX;
    }
    if (power_mw <= -(int32_t)INT16_MAX * 1000) {
        return -INT16_MAX;
    }
    return (int16_t)(power_mw >= 0 ? (power_mw + 500) / 1000 : -((500 - power_mw) / 1000));
}

/* Whole Wh of an energy total, saturated to the uint48 range. */
static inline uint64_t meter_units_energy_wh(uint64_t energy_uwh) {
    uint64_t wh = fixed_div_u64(energy_uwh, 1000000u, 0);

    return wh > METER_UNITS_MAX_U48 ? METER_UNITS_MAX_U48 : wh;
}

#endif /* ZIGBEE_METER_UNITS_H_ */
