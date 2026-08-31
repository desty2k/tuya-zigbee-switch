#include "telink_size_t_hack.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

#include "hal/zigbee.h"
#include "hal/zigbee_ota.h"
#include "telink_zigbee_hal.h"

/**
 * Main Zigbee HAL initialization - coordinates between network and ZCL modules
 */
void hal_zigbee_init(hal_zigbee_endpoint *endpoints, uint8_t endpoints_cnt) {
    // Initialize network layer (BDB, ZDO callbacks, basic Zigbee stack)
    telink_zigbee_hal_network_init();

    // Initialize ZCL layer (clusters, attributes, endpoints)
    telink_zigbee_hal_zcl_init(endpoints, endpoints_cnt);

    // OTA registers its SDK handler against the initialized descriptors.
    hal_zigbee_init_ota();

    // Start BDB only after OTA can handle callbacks and incoming commands.
    telink_zigbee_hal_bdb_init(telink_zigbee_hal_zcl_get_descriptors());
}

/**
 * Get endpoint descriptors - delegates to ZCL module
 */
af_simple_descriptor_t *telink_zigbee_hal_get_descriptors(void) {
    return telink_zigbee_hal_zcl_get_descriptors();
}
