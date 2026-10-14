#ifndef BASE_COMPONENTS_METER_FAKE_DRIVER_H_
#define BASE_COMPONENTS_METER_FAKE_DRIVER_H_

#include "base_components/energy_meter.h"

typedef struct {
    meter_sample_t sample;
    bool           available;
} meter_fake_driver_t;

void meter_fake_driver_init(meter_fake_driver_t *driver);
void meter_fake_driver_set_sample(meter_fake_driver_t *driver,
                                  const meter_sample_t *sample);
void meter_fake_driver_set_available(meter_fake_driver_t *driver, bool available);
meter_driver_t meter_fake_driver_as_meter(meter_fake_driver_t *driver);

#endif /* BASE_COMPONENTS_METER_FAKE_DRIVER_H_ */
