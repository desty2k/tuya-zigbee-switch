#include "telink_size_t_hack.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

#include "hal/zigbee.h"
#include "hal/zigbee_ota.h"
#include "telink_zigbee_hal.h"

static bool network_initialized;

/**
 * Main Zigbee HAL initialization - coordinates between network and ZCL modules.
 * A rejected graph registers nothing, so the caller can initialize a smaller
 * graph; the network layer is initialized once for both attempts.
 */
bool hal_zigbee_init(hal_zigbee_endpoint *endpoints, uint8_t endpoints_cnt) {
    if (!telink_zigbee_hal_zcl_graph_supported(endpoints, endpoints_cnt)) {
        return false;
    }

    // Initialize network layer (BDB, ZDO callbacks, basic Zigbee stack)
    if (!network_initialized) {
        telink_zigbee_hal_network_init();
        network_initialized = true;
    }

    // Initialize ZCL layer (clusters, attributes, endpoints)
    if (!telink_zigbee_hal_zcl_init(endpoints, endpoints_cnt)) {
        return false;
    }

    // OTA registers its SDK handler against the initialized descriptors.
    hal_zigbee_init_ota();

    // Start BDB only after OTA can handle callbacks and incoming commands.
    telink_zigbee_hal_bdb_init(telink_zigbee_hal_zcl_get_descriptors());
    return true;
}

/**
 * Get endpoint descriptors - delegates to ZCL module
 */
af_simple_descriptor_t *telink_zigbee_hal_get_descriptors(void) {
    return telink_zigbee_hal_zcl_get_descriptors();
}
