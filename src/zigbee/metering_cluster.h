#ifndef ZIGBEE_METERING_CLUSTER_H_
#define ZIGBEE_METERING_CLUSTER_H_

#include "hal/zigbee.h"
#include <stdint.h>

/* Metering (0x0702) projection of one meter_service energy total. */
typedef struct {
    uint8_t              meter_id;
    uint8_t              endpoint;
    uint8_t              summation_delivered[6]; /* uint48, little-endian Wh */
    uint8_t              status;
    uint8_t              unit_of_measure;
    uint8_t              multiplier[3];          /* uint24 */
    uint8_t              divisor[3];             /* uint24 */
    uint8_t              summation_formatting;
    uint8_t              device_type;
    hal_zigbee_attribute attr_infos[7];
} zigbee_metering_cluster;

void metering_cluster_add_to_endpoint(zigbee_metering_cluster *cluster, uint8_t meter_id,
                                      hal_zigbee_endpoint *endpoint);

#endif
