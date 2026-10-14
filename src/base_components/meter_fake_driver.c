#include "meter_fake_driver.h"

#include <string.h>

static bool meter_fake_sample(void *context, meter_sample_t *out) {
    meter_fake_driver_t *driver = context;

    if (driver == NULL || out == NULL || !driver->available) {
        return false;
    }
    *out = driver->sample;
    return true;
}

static void meter_fake_reset_energy_remainder(void *context) {
    meter_fake_driver_t *driver = context;

    if (driver != NULL) {
        driver->sample.energy_delta_uwh = 0;
    }
}

static const meter_driver_ops_t meter_fake_driver_ops = {
    .sample                 = meter_fake_sample,
    .reset_energy_remainder = meter_fake_reset_energy_remainder,
};

void meter_fake_driver_init(meter_fake_driver_t *driver) {
    if (driver != NULL) {
        memset(driver, 0, sizeof(*driver));
        driver->available = true;
    }
}

void meter_fake_driver_set_sample(meter_fake_driver_t *driver,
                                  const meter_sample_t *sample) {
    if (driver != NULL && sample != NULL) {
        driver->sample = *sample;
    }
}

void meter_fake_driver_set_available(meter_fake_driver_t *driver, bool available) {
    if (driver != NULL) {
        driver->available = available;
    }
}

meter_driver_t meter_fake_driver_as_meter(meter_fake_driver_t *driver) {
    return (meter_driver_t){
               .ops     = &meter_fake_driver_ops,
               .context = driver,
    };
}
