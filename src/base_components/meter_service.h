#ifndef BASE_COMPONENTS_METER_SERVICE_H_
#define BASE_COMPONENTS_METER_SERVICE_H_

#include "base_components/energy_meter.h"
#include <stdbool.h>
#include <stdint.h>

#define METER_SERVICE_MAX_INSTANCES      2
#define METER_SERVICE_MAX_SUBSCRIBERS    4

typedef enum {
    METER_SERVICE_OK,
    METER_SERVICE_INVALID,
    METER_SERVICE_NVM_ERROR,
} meter_service_result_t;

typedef struct {
    meter_driver_t      driver;
    meter_calibration_t profile_calibration;
    uint32_t            sample_interval_ms;
    uint32_t            checkpoint_interval_ms;
    uint64_t            checkpoint_energy_uwh;
} meter_service_config_t;

typedef struct {
    meter_sample_t      sample;
    meter_calibration_t calibration;
    uint64_t            accumulated_energy_uwh;
    uint8_t             runtime_calibration_active;
} meter_service_snapshot_t;

typedef void (*meter_service_subscriber_t)(uint8_t meter_id,
                                           const meter_service_snapshot_t *snapshot,
                                           void *context);

void meter_service_init(void);
uint8_t meter_service_add(const meter_service_config_t *config);
bool meter_service_start(uint8_t meter_id);
void meter_service_stop(uint8_t meter_id);
bool meter_service_poll(uint8_t meter_id);
bool meter_service_snapshot(uint8_t meter_id, meter_service_snapshot_t *out);
bool meter_service_subscribe(uint8_t meter_id, meter_service_subscriber_t callback,
                             void *context);
meter_service_result_t meter_service_set_calibration(
    uint8_t meter_id, const meter_calibration_t *calibration);
meter_service_result_t meter_service_clear_calibration(uint8_t meter_id);
meter_service_result_t meter_service_reset_energy(uint8_t meter_id);
void meter_service_clear_owned(void);

#endif /* BASE_COMPONENTS_METER_SERVICE_H_ */
