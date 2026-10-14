#ifndef ZIGBEE_DEVICE_DIAGNOSTICS_CLUSTER_H_
#define ZIGBEE_DEVICE_DIAGNOSTICS_CLUSTER_H_

#include "hal/zigbee.h"

typedef struct {
    hal_zigbee_attribute attr_infos[20];
    uint32_t             capabilities;
    uint32_t             values[18];
    uint8_t              last_descriptor_error;
} zigbee_device_diagnostics_cluster_t;

void device_diagnostics_cluster_init(zigbee_device_diagnostics_cluster_t *cluster);
void device_diagnostics_cluster_refresh(void);
void device_diagnostics_cluster_add_to_endpoint(
    zigbee_device_diagnostics_cluster_t *cluster, hal_zigbee_endpoint *endpoint);

#endif
