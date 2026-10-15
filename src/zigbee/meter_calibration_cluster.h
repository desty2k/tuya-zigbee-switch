#ifndef ZIGBEE_METER_CALIBRATION_CLUSTER_H_
#define ZIGBEE_METER_CALIBRATION_CLUSTER_H_

#include "hal/zigbee.h"
#include <stdint.h>

/*
 * Fork-private Meter Calibration cluster (0xFC12, management manufacturer
 * code): the meter service's effective calibration gains in ppm (1000000 =
 * 1.0, writable 500000..1500000, persisted by the service) and the Reset
 * Energy command (0x00, no payload).
 */
typedef struct {
    uint8_t              meter_id;
    uint8_t              endpoint;
    uint32_t             voltage_gain_ppm;
    uint32_t             current_gain_ppm;
    uint32_t             power_gain_ppm;
    hal_zigbee_attribute attr_infos[3];
} zigbee_meter_calibration_cluster;

void meter_calibration_cluster_add_to_endpoint(zigbee_meter_calibration_cluster *cluster,
                                               uint8_t meter_id,
                                               hal_zigbee_endpoint *endpoint);

#endif
