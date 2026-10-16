#ifndef ZIGBEE_DEVICE_CONFIG_CLUSTER_H_
#define ZIGBEE_DEVICE_CONFIG_CLUSTER_H_

#include "hal/zigbee.h"

typedef struct {
    hal_zigbee_attribute attr_infos[8];
    uint16_t             schema_version;
    uint32_t             revision;
    struct {
        uint16_t length;
        char     data[126];
    }                    config;
    uint8_t              status;
    uint8_t              error;
    uint8_t              led_mode;
    uint8_t              multi_press_reset;
    uint8_t              child_lock;
    uint8_t              endpoint;
} zigbee_device_config_cluster_t;

void device_config_cluster_init(zigbee_device_config_cluster_t *cluster);
void device_config_cluster_add_to_endpoint(zigbee_device_config_cluster_t *cluster,
                                           hal_zigbee_endpoint *endpoint);

#endif
