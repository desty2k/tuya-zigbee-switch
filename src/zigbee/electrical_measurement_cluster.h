#ifndef ZIGBEE_ELECTRICAL_MEASUREMENT_CLUSTER_H_
#define ZIGBEE_ELECTRICAL_MEASUREMENT_CLUSTER_H_

#include "hal/zigbee.h"
#include <stdint.h>

/* Electrical Measurement (0x0B04) projection of one meter_service meter. */
typedef struct {
    uint8_t              meter_id;
    uint8_t              endpoint;
    uint32_t             measurement_type;
    uint16_t             rms_voltage;
    uint16_t             rms_current;
    int16_t              active_power;
    uint16_t             voltage_multiplier;
    uint16_t             voltage_divisor;
    uint16_t             current_multiplier;
    uint16_t             current_divisor;
    uint16_t             power_multiplier;
    uint16_t             power_divisor;
    hal_zigbee_attribute attr_infos[10];
} zigbee_electrical_measurement_cluster;

void electrical_measurement_cluster_add_to_endpoint(
    zigbee_electrical_measurement_cluster *cluster, uint8_t meter_id,
    hal_zigbee_endpoint *endpoint);

#endif
