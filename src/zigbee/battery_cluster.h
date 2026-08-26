#ifndef _BATTERY_CLUSTER_H_
#define _BATTERY_CLUSTER_H_

#include "base_components/timer_service.h"
#include "hal/zigbee.h"

typedef struct {
    uint8_t              voltage_100mv;        // ZCL BatteryVoltage in 100mV units
    uint8_t              percentage_remaining; // ZCL 0-200 (0.5% steps)
    uint8_t              endpoint;
    app_timer_t          refresh_values_timer;
    hal_zigbee_attribute attr_infos[3];        // voltage + percentage + cluster_revision
} zigbee_battery_cluster;

void battery_cluster_add_to_endpoint(zigbee_battery_cluster *cluster,
                                     hal_zigbee_endpoint *endpoint);
void battery_cluster_update(zigbee_battery_cluster *cluster);

#endif // _BATTERY_CLUSTER_H_
