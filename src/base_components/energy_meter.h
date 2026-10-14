#ifndef BASE_COMPONENTS_ENERGY_METER_H_
#define BASE_COMPONENTS_ENERGY_METER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define METER_SAMPLE_VOLTAGE           (1U << 0)
#define METER_SAMPLE_CURRENT           (1U << 1)
#define METER_SAMPLE_ACTIVE_POWER      (1U << 2)
#define METER_SAMPLE_APPARENT_POWER    (1U << 3)
#define METER_SAMPLE_REACTIVE_POWER    (1U << 4)
#define METER_SAMPLE_POWER_FACTOR      (1U << 5)
#define METER_SAMPLE_ENERGY            (1U << 6)

#define METER_CALIBRATION_UNITY_PPM    1000000UL
#define METER_CALIBRATION_MIN_PPM      500000UL
#define METER_CALIBRATION_MAX_PPM      1500000UL

typedef struct {
    uint32_t voltage_mv;
    uint32_t current_ma;
    int32_t  active_power_mw;
    uint32_t apparent_power_mva;
    int32_t  reactive_power_mvar;
    int16_t  power_factor_milli;
    uint64_t energy_delta_uwh;
    uint16_t valid_mask;
} meter_sample_t;

typedef struct {
    uint32_t voltage_gain_ppm;
    uint32_t current_gain_ppm;
    uint32_t power_gain_ppm;
} meter_calibration_t;

typedef struct {
    bool (*sample)(void *context, meter_sample_t *out);
    void (*reset_energy_remainder)(void *context);
} meter_driver_ops_t;

typedef struct {
    const meter_driver_ops_t *ops;
    void *                    context;
} meter_driver_t;

static inline bool energy_meter_sample(const meter_driver_t *driver,
                                       meter_sample_t *out) {
    return driver != NULL && driver->ops != NULL &&
           driver->ops->sample != NULL && driver->ops->sample(driver->context, out);
}

static inline void energy_meter_reset_remainder(const meter_driver_t *driver) {
    if (driver != NULL && driver->ops != NULL &&
        driver->ops->reset_energy_remainder != NULL) {
        driver->ops->reset_energy_remainder(driver->context);
    }
}

#endif /* BASE_COMPONENTS_ENERGY_METER_H_ */
