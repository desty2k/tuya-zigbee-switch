#ifndef ZIGBEE_COVER_CLUSTER_H_
#define ZIGBEE_COVER_CLUSTER_H_

#include "hal/zigbee.h"
#include <stdint.h>

typedef struct {
    uint8_t              controller_id;
    uint8_t              endpoint;
    uint8_t              moving;
    uint8_t              motor_reversal;
    uint8_t              position;
    uint32_t             open_time_ms;
    uint32_t             close_time_ms;
    uint16_t             closed_deadzone_bp;
    uint16_t             open_deadzone_bp;
    uint16_t             overrun_ms;
    hal_zigbee_attribute attr_infos[9];
} zigbee_cover_cluster;

void cover_cluster_add_to_endpoint(zigbee_cover_cluster *cluster, hal_zigbee_endpoint *endpoint);

#endif
