#include "electrical_measurement_cluster.h"

#include "cluster_common.h"
#include "meter_units.h"
#include "zcl_cluster_defs.h"
#include "zcl_ids.h"
#include "zcl_types.h"
#include "base_components/meter_service.h"
#include <string.h>

static void set_u16(zigbee_electrical_measurement_cluster *cluster, uint16_t *field,
                    uint16_t value, uint16_t attribute_id) {
    if (*field != value) {
        *field = value;
        hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                            ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                                            attribute_id);
    }
}

/* Invalid quantities keep the ZCL invalid value; they are never estimated. */
static void meter_changed(uint8_t meter_id, const meter_service_snapshot_t *snapshot,
                          void *context) {
    zigbee_electrical_measurement_cluster *cluster = context;
    const meter_sample_t *sample = &snapshot->sample;
    int16_t power = METER_UNITS_INVALID_S16;

    (void)meter_id;
    if (sample->valid_mask & METER_SAMPLE_VOLTAGE) {
        set_u16(cluster, &cluster->rms_voltage, meter_units_voltage_dv(sample->voltage_mv),
                ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE);
    }
    if (sample->valid_mask & METER_SAMPLE_CURRENT) {
        set_u16(cluster, &cluster->rms_current, meter_units_current_ma(sample->current_ma),
                ZCL_ATTR_ELEC_MEAS_RMS_CURRENT);
    }
    if (sample->valid_mask & METER_SAMPLE_ACTIVE_POWER) {
        power = meter_units_power_w(sample->active_power_mw);
    }
    if (power != cluster->active_power) {
        cluster->active_power = power;
        hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                            ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                                            ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER);
    }
}

void electrical_measurement_cluster_add_to_endpoint(
    zigbee_electrical_measurement_cluster *cluster, uint8_t meter_id,
    hal_zigbee_endpoint *endpoint) {
    meter_service_snapshot_t snapshot;

    if (!hal_zigbee_endpoint_reserve_clusters(endpoint, endpoint->cluster_capacity, 1)) {
        return;
    }
    memset(cluster, 0, sizeof(*cluster));
    cluster->meter_id           = meter_id;
    cluster->endpoint           = endpoint->endpoint;
    cluster->measurement_type   = ZCL_ELEC_MEAS_TYPE_ACTIVE_AC;
    cluster->rms_voltage        = METER_UNITS_INVALID_U16;
    cluster->rms_current        = METER_UNITS_INVALID_U16;
    cluster->active_power       = METER_UNITS_INVALID_S16;
    cluster->voltage_multiplier = 1;
    cluster->voltage_divisor    = METER_UNITS_VOLTAGE_DIVISOR;
    cluster->current_multiplier = 1;
    cluster->current_divisor    = METER_UNITS_CURRENT_DIVISOR;
    cluster->power_multiplier   = 1;
    cluster->power_divisor      = METER_UNITS_POWER_DIVISOR;
    SETUP_ATTR(0, ZCL_ATTR_ELEC_MEAS_MEASUREMENT_TYPE, ZCL_DATA_TYPE_BITMAP32,
               HAL_ATTR_READ, cluster->measurement_type);
    SETUP_ATTR(1, ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ | HAL_ATTR_REPORT, cluster->rms_voltage);
    SETUP_ATTR(2, ZCL_ATTR_ELEC_MEAS_RMS_CURRENT, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ | HAL_ATTR_REPORT, cluster->rms_current);
    SETUP_ATTR(3, ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER, ZCL_DATA_TYPE_INT16,
               HAL_ATTR_READ | HAL_ATTR_REPORT, cluster->active_power);
    SETUP_ATTR(4, ZCL_ATTR_ELEC_MEAS_AC_VOLTAGE_MULTIPLIER, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster->voltage_multiplier);
    SETUP_ATTR(5, ZCL_ATTR_ELEC_MEAS_AC_VOLTAGE_DIVISOR, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster->voltage_divisor);
    SETUP_ATTR(6, ZCL_ATTR_ELEC_MEAS_AC_CURRENT_MULTIPLIER, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster->current_multiplier);
    SETUP_ATTR(7, ZCL_ATTR_ELEC_MEAS_AC_CURRENT_DIVISOR, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster->current_divisor);
    SETUP_ATTR(8, ZCL_ATTR_ELEC_MEAS_AC_POWER_MULTIPLIER, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster->power_multiplier);
    SETUP_ATTR(9, ZCL_ATTR_ELEC_MEAS_AC_POWER_DIVISOR, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster->power_divisor);
    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id      = ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
        .role            = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count =                                 10,
        .attributes      = cluster->attr_infos,
    };
    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity, &endpoint_cluster);
    meter_service_subscribe(meter_id, meter_changed, cluster);
    if (meter_service_snapshot(meter_id, &snapshot)) {
        meter_changed(meter_id, &snapshot, cluster);
    }
}
