#include "device_diagnostics_cluster.h"

#include "base_components/diagnostics_service.h"
#include "cluster_common.h"
#include "zcl_types.h"
#include "management_protocol.h"
#include <string.h>

static zigbee_device_diagnostics_cluster_t *instance;

void device_diagnostics_cluster_refresh(void) {
    const diagnostics_snapshot_t *snapshot = diagnostics_service_get_snapshot();

    if (instance == NULL) return;

    instance->values[0]             = snapshot->uptime_ms;
    instance->values[1]             = snapshot->gpio_edges_captured;
    instance->values[2]             = snapshot->gpio_edges_dropped;
    instance->values[3]             = snapshot->gpio_rearm_limit_hits;
    instance->values[4]             = snapshot->button_events_emitted;
    instance->values[5]             = snapshot->gestures_emitted;
    instance->values[6]             = snapshot->zb_submitted;
    instance->values[7]             = snapshot->zb_dropped;
    instance->values[8]             = snapshot->zb_expired;
    instance->values[9]             = snapshot->zb_send_failed;
    instance->values[10]            = snapshot->zb_high_water;
    instance->values[11]            = snapshot->network_transitions;
    instance->values[12]            = snapshot->network_losses;
    instance->values[13]            = snapshot->network_joins;
    instance->values[14]            = snapshot->announce_attempts;
    instance->values[15]            = snapshot->announce_failures;
    instance->values[16]            = snapshot->steering_attempts;
    instance->values[17]            = snapshot->descriptor_validation_failures;
    instance->last_descriptor_error = snapshot->last_descriptor_error;
}

void device_diagnostics_cluster_init(zigbee_device_diagnostics_cluster_t *cluster) {
    memset(cluster, 0, sizeof(*cluster));
    instance = cluster;
    cluster->capabilities = DIAGNOSTICS_CAPABILITY_CORE;
#if FEATURE_PROBE
    cluster->capabilities |= DIAGNOSTICS_CAPABILITY_PROBE;
#endif
    device_diagnostics_cluster_refresh();
}

void device_diagnostics_cluster_add_to_endpoint(
    zigbee_device_diagnostics_cluster_t *cluster, hal_zigbee_endpoint *endpoint) {
    static const uint16_t ids[] = {
        ZCL_ATTR_DIAGNOSTICS_UPTIME_MS,      ZCL_ATTR_DIAGNOSTICS_GPIO_EDGES,
        ZCL_ATTR_DIAGNOSTICS_GPIO_DROPPED,   ZCL_ATTR_DIAGNOSTICS_GPIO_REARM_HITS,
        ZCL_ATTR_DIAGNOSTICS_BUTTON_EVENTS,  ZCL_ATTR_DIAGNOSTICS_GESTURES,
        ZCL_ATTR_DIAGNOSTICS_ZB_SUBMITTED,   ZCL_ATTR_DIAGNOSTICS_ZB_DROPPED,
        ZCL_ATTR_DIAGNOSTICS_ZB_EXPIRED,     ZCL_ATTR_DIAGNOSTICS_ZB_SEND_FAILED,
        ZCL_ATTR_DIAGNOSTICS_ZB_HIGH_WATER,  ZCL_ATTR_DIAGNOSTICS_NETWORK_TRANSITIONS,
        ZCL_ATTR_DIAGNOSTICS_NETWORK_LOSSES, ZCL_ATTR_DIAGNOSTICS_NETWORK_JOINS,
        ZCL_ATTR_DIAGNOSTICS_ANNOUNCES,      ZCL_ATTR_DIAGNOSTICS_ANNOUNCE_FAILURES,
        ZCL_ATTR_DIAGNOSTICS_STEERING,       ZCL_ATTR_DIAGNOSTICS_DESCRIPTOR_FAILURES,
    };

    if (!hal_zigbee_endpoint_reserve_clusters(endpoint,
                                              endpoint->cluster_capacity, 1)) return;

    instance = cluster;
    device_diagnostics_cluster_refresh();
    SETUP_ATTR(0, ZCL_ATTR_DIAGNOSTICS_CAPABILITIES, ZCL_DATA_TYPE_BITMAP32,
               HAL_ATTR_READ, cluster->capabilities);
    // Uptime changes on every refresh, so it is read-only and never reported.
    SETUP_ATTR(1, ZCL_ATTR_DIAGNOSTICS_UPTIME_MS, ZCL_DATA_TYPE_UINT32, HAL_ATTR_READ, cluster->values[0]);
    for (uint8_t i = 1; i < 18; i++) {
        SETUP_ATTR_FOR_TABLE(cluster->attr_infos, i + 1, ids[i], ZCL_DATA_TYPE_UINT32,
                             HAL_ATTR_READ | HAL_ATTR_REPORT, cluster->values[i]);
    }
    SETUP_ATTR(19, ZCL_ATTR_DIAGNOSTICS_LAST_DESCRIPTOR_ERROR,
               ZCL_DATA_TYPE_ENUM8, HAL_ATTR_READ, cluster->last_descriptor_error);
#if FEATURE_PROBE
    gpio_probe_cluster_setup(&cluster->probe,
                             &cluster->attr_infos[DEVICE_DIAGNOSTICS_CORE_ATTRIBUTES]);
#endif
    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id        = ZCL_CLUSTER_DEVICE_DIAGNOSTICS,
        .manufacturer_code = ZIGBEE_MANAGEMENT_MANUFACTURER_CODE,
        .role            = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count = DEVICE_DIAGNOSTICS_ATTRIBUTES,
        .attributes      = cluster->attr_infos,
#if FEATURE_PROBE
        .cmd_callback      = gpio_probe_cluster_command,
#endif
    };
    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity,
                                    &endpoint_cluster);
}
