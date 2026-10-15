#include "metering_cluster.h"

#include "cluster_common.h"
#include "meter_units.h"
#include "zcl_cluster_defs.h"
#include "zcl_ids.h"
#include "zcl_types.h"
#include "base_components/meter_service.h"
#include <string.h>

/* 5 digits left of the decimal point, 3 right: kWh with Wh resolution. */
#define METERING_SUMMATION_FORMATTING    ((5u << 3) | 3u)

static void put_u24(uint8_t out[3], uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
}

static void meter_changed(uint8_t meter_id, const meter_service_snapshot_t *snapshot,
                          void *context) {
    zigbee_metering_cluster *cluster = context;
    uint64_t wh = meter_units_energy_wh(snapshot->accumulated_energy_uwh);
    uint8_t  encoded[6];

    (void)meter_id;
    for (uint8_t i = 0; i < sizeof(encoded); i++) {
        encoded[i] = (uint8_t)wh;
        wh       >>= 8;
    }
    if (memcmp(encoded, cluster->summation_delivered, sizeof(encoded)) != 0) {
        memcpy(cluster->summation_delivered, encoded, sizeof(encoded));
        hal_zigbee_notify_attribute_changed(cluster->endpoint, ZCL_CLUSTER_METERING,
                                            ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED);
    }
}

void metering_cluster_add_to_endpoint(zigbee_metering_cluster *cluster, uint8_t meter_id,
                                      hal_zigbee_endpoint *endpoint) {
    meter_service_snapshot_t snapshot;

    if (!hal_zigbee_endpoint_reserve_clusters(endpoint, endpoint->cluster_capacity, 1)) {
        return;
    }
    memset(cluster, 0, sizeof(*cluster));
    cluster->meter_id             = meter_id;
    cluster->endpoint             = endpoint->endpoint;
    cluster->unit_of_measure      = ZCL_METERING_UNIT_KWH;
    cluster->summation_formatting = METERING_SUMMATION_FORMATTING;
    cluster->device_type          = ZCL_METERING_DEVICE_TYPE_ELECTRIC;
    put_u24(cluster->multiplier, 1);
    put_u24(cluster->divisor, METER_UNITS_ENERGY_DIVISOR);
    SETUP_ATTR(0, ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED, ZCL_DATA_TYPE_UINT48,
               HAL_ATTR_READ | HAL_ATTR_REPORT, cluster->summation_delivered);
    SETUP_ATTR(1, ZCL_ATTR_METERING_STATUS, ZCL_DATA_TYPE_BITMAP8, HAL_ATTR_READ,
               cluster->status);
    SETUP_ATTR(2, ZCL_ATTR_METERING_UNIT_OF_MEASURE, ZCL_DATA_TYPE_ENUM8, HAL_ATTR_READ,
               cluster->unit_of_measure);
    SETUP_ATTR(3, ZCL_ATTR_METERING_MULTIPLIER, ZCL_DATA_TYPE_UINT24, HAL_ATTR_READ,
               cluster->multiplier);
    SETUP_ATTR(4, ZCL_ATTR_METERING_DIVISOR, ZCL_DATA_TYPE_UINT24, HAL_ATTR_READ,
               cluster->divisor);
    SETUP_ATTR(5, ZCL_ATTR_METERING_SUMMATION_FORMATTING, ZCL_DATA_TYPE_BITMAP8,
               HAL_ATTR_READ, cluster->summation_formatting);
    SETUP_ATTR(6, ZCL_ATTR_METERING_DEVICE_TYPE, ZCL_DATA_TYPE_BITMAP8, HAL_ATTR_READ,
               cluster->device_type);
    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id      = ZCL_CLUSTER_METERING,
        .role            = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count =                      7,
        .attributes      = cluster->attr_infos,
    };
    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity, &endpoint_cluster);
    meter_service_subscribe(meter_id, meter_changed, cluster);
    if (meter_service_snapshot(meter_id, &snapshot)) {
        meter_changed(meter_id, &snapshot, cluster);
    }
}
