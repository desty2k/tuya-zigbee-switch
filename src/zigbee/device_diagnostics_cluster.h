#ifndef ZIGBEE_DEVICE_DIAGNOSTICS_CLUSTER_H_
#define ZIGBEE_DEVICE_DIAGNOSTICS_CLUSTER_H_

#include "device_config/build_features.h"
#include "hal/zigbee.h"
#if FEATURE_PROBE
#include "zigbee/gpio_probe_cluster.h"
#endif

#define DEVICE_DIAGNOSTICS_CORE_ATTRIBUTES    20
#if FEATURE_PROBE
#define DEVICE_DIAGNOSTICS_ATTRIBUTES \
        (DEVICE_DIAGNOSTICS_CORE_ATTRIBUTES + GPIO_PROBE_CLUSTER_ATTRIBUTE_COUNT)
#else
#define DEVICE_DIAGNOSTICS_ATTRIBUTES    DEVICE_DIAGNOSTICS_CORE_ATTRIBUTES
#endif

typedef struct {
    hal_zigbee_attribute      attr_infos[DEVICE_DIAGNOSTICS_ATTRIBUTES];
    uint32_t                  capabilities;
    uint32_t                  values[18];
    uint8_t                   last_descriptor_error;
#if FEATURE_PROBE
    zigbee_gpio_probe_attrs_t probe;
#endif
} zigbee_device_diagnostics_cluster_t;

void device_diagnostics_cluster_init(zigbee_device_diagnostics_cluster_t *cluster);
void device_diagnostics_cluster_refresh(void);
void device_diagnostics_cluster_add_to_endpoint(
    zigbee_device_diagnostics_cluster_t *cluster, hal_zigbee_endpoint *endpoint);

#endif
