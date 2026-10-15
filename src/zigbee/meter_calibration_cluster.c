#include "meter_calibration_cluster.h"

#include "cluster_common.h"
#include "zcl_cluster_defs.h"
#include "zcl_ids.h"
#include "zcl_types.h"
#include "base_components/meter_service.h"
#include <string.h>

#define METER_CALIBRATION_MAX_CLUSTERS    METER_SERVICE_MAX_INSTANCES

static zigbee_meter_calibration_cluster *instances[METER_CALIBRATION_MAX_CLUSTERS];

static zigbee_meter_calibration_cluster *find_by_endpoint(uint8_t endpoint) {
    for (uint8_t i = 0; i < METER_CALIBRATION_MAX_CLUSTERS; i++) {
        if (instances[i] != NULL && instances[i]->endpoint == endpoint) {
            return instances[i];
        }
    }
    return NULL;
}

static void meter_changed(uint8_t meter_id, const meter_service_snapshot_t *snapshot,
                          void *context) {
    zigbee_meter_calibration_cluster *cluster = context;

    (void)meter_id;
    cluster->voltage_gain_ppm = snapshot->calibration.voltage_gain_ppm;
    cluster->current_gain_ppm = snapshot->calibration.current_gain_ppm;
    cluster->power_gain_ppm   = snapshot->calibration.power_gain_ppm;
}

static uint32_t *gain_for(meter_calibration_t *calibration, uint16_t attribute_id) {
    switch (attribute_id) {
    case ZCL_ATTR_METER_CALIBRATION_VOLTAGE_GAIN:
        return &calibration->voltage_gain_ppm;

    case ZCL_ATTR_METER_CALIBRATION_CURRENT_GAIN:
        return &calibration->current_gain_ppm;

    case ZCL_ATTR_METER_CALIBRATION_POWER_GAIN:
        return &calibration->power_gain_ppm;

    default:
        return NULL;
    }
}

static hal_zigbee_cmd_result_t attr_write(uint8_t endpoint, uint16_t attribute_id,
                                          const uint8_t *candidate, uint16_t candidate_len,
                                          hal_zigbee_attr_write_phase_t phase) {
    zigbee_meter_calibration_cluster *cluster = find_by_endpoint(endpoint);
    meter_service_snapshot_t          snapshot;
    meter_calibration_t calibration;
    uint32_t *          gain;
    uint32_t            value;

    if (cluster == NULL || !meter_service_snapshot(cluster->meter_id, &snapshot)) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    calibration = snapshot.calibration;
    gain        = gain_for(&calibration, attribute_id);
    if (gain == NULL) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    if (candidate == NULL || candidate_len != sizeof(uint32_t)) {
        return HAL_ZIGBEE_INVALID_VALUE;
    }
    value = (uint32_t)candidate[0] | ((uint32_t)candidate[1] << 8) |
            ((uint32_t)candidate[2] << 16) | ((uint32_t)candidate[3] << 24);
    if (value < METER_CALIBRATION_MIN_PPM || value > METER_CALIBRATION_MAX_PPM) {
        return HAL_ZIGBEE_INVALID_VALUE;
    }
    if (phase == HAL_ZIGBEE_ATTR_VALIDATE) {
        return HAL_ZIGBEE_CMD_PROCESSED;
    }
    *gain = value;
    if (meter_service_set_calibration(cluster->meter_id, &calibration) != METER_SERVICE_OK) {
        return HAL_ZIGBEE_ACTION_DENIED;
    }
    /* A write callback stores the committed value itself. */
    cluster->voltage_gain_ppm = calibration.voltage_gain_ppm;
    cluster->current_gain_ppm = calibration.current_gain_ppm;
    cluster->power_gain_ppm   = calibration.power_gain_ppm;
    return HAL_ZIGBEE_CMD_PROCESSED;
}

static hal_zigbee_cmd_result_t command(const hal_zigbee_incoming_command_t *cmd) {
    zigbee_meter_calibration_cluster *cluster = find_by_endpoint(cmd->destination_endpoint);

    if (cluster == NULL || !cmd->cluster_specific ||
        cmd->direction != HAL_ZIGBEE_DIR_CLIENT_TO_SERVER ||
        cmd->command_id != ZCL_CMD_METER_CALIBRATION_RESET_ENERGY) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    if (cmd->payload_len != 0) {
        return HAL_ZIGBEE_MALFORMED_COMMAND;
    }
    return meter_service_reset_energy(cluster->meter_id) == METER_SERVICE_OK ?
           HAL_ZIGBEE_CMD_PROCESSED : HAL_ZIGBEE_ACTION_DENIED;
}

void meter_calibration_cluster_add_to_endpoint(zigbee_meter_calibration_cluster *cluster,
                                               uint8_t meter_id,
                                               hal_zigbee_endpoint *endpoint) {
    meter_service_snapshot_t snapshot;

    if (meter_id >= METER_CALIBRATION_MAX_CLUSTERS ||
        !hal_zigbee_endpoint_reserve_clusters(endpoint, endpoint->cluster_capacity, 1)) {
        return;
    }
    memset(cluster, 0, sizeof(*cluster));
    cluster->meter_id   = meter_id;
    cluster->endpoint   = endpoint->endpoint;
    instances[meter_id] = cluster;
    SETUP_ATTR(0, ZCL_ATTR_METER_CALIBRATION_VOLTAGE_GAIN, ZCL_DATA_TYPE_UINT32,
               HAL_ATTR_READ | HAL_ATTR_WRITE, cluster->voltage_gain_ppm);
    SETUP_ATTR(1, ZCL_ATTR_METER_CALIBRATION_CURRENT_GAIN, ZCL_DATA_TYPE_UINT32,
               HAL_ATTR_READ | HAL_ATTR_WRITE, cluster->current_gain_ppm);
    SETUP_ATTR(2, ZCL_ATTR_METER_CALIBRATION_POWER_GAIN, ZCL_DATA_TYPE_UINT32,
               HAL_ATTR_READ | HAL_ATTR_WRITE, cluster->power_gain_ppm);
    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id        = ZCL_CLUSTER_METER_CALIBRATION,
        .manufacturer_code = ZIGBEE_MANAGEMENT_MANUFACTURER_CODE,
        .role                = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count     =                                   3,
        .attributes          = cluster->attr_infos,
        .cmd_callback        = command,
        .attr_write_callback = attr_write,
    };
    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity, &endpoint_cluster);
    meter_service_subscribe(meter_id, meter_changed, cluster);
    if (meter_service_snapshot(meter_id, &snapshot)) {
        meter_changed(meter_id, &snapshot, cluster);
    }
}
