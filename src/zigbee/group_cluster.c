#include "group_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/zigbee.h"
#include <stdint.h>

const uint8_t groupNameSupport = 0x0;

void group_cluster_add_to_endpoint(zigbee_group_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    if (!hal_zigbee_endpoint_reserve_clusters(endpoint,
                                              endpoint->cluster_capacity, 1)) {
        return;
    }

    SETUP_ATTR(0, ZCL_ATTR_GROUP_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8,
               ATTR_READONLY, groupNameSupport);

    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id      = ZCL_CLUSTER_GROUPS,
        .is_server       =                   1,
        .attribute_count =                   1,
        .attributes      = cluster->attr_infos,
    };

    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity,
                                    &endpoint_cluster);
}
