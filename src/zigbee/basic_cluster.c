#include "basic_cluster.h"

#include "build_date.h"
#include "cluster_common.h"
#include "zcl_cluster_defs.h"
#include "zcl_ids.h"
#include "zcl_types.h"
#include "device_config/feature_wiring.h"
#include "hal/gpio.h"

static const uint8_t  zcl_version      = 3;
static const uint8_t  app_version      = 3;
static const uint8_t  stack_version    = 2;
static const uint8_t  hw_version       = 0;
static const uint16_t cluster_revision = 1;
static uint8_t        power_source     = POWER_SOURCE_MAINS_1_PHASE;
DEF_STR(STRINGIFY_VALUE(VERSION_STR), sw_build_id);

void basic_cluster_add_to_endpoint(zigbee_basic_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    if (!hal_zigbee_endpoint_reserve_clusters(endpoint,
                                              endpoint->cluster_capacity, 1)) return;

    if (battery.pin != HAL_INVALID_PIN) power_source = POWER_SOURCE_BATTERY;
    zb_build_date_init(ZB_BUILD_DATE_YYYYMMDD);

    SETUP_ATTR(0, ZCL_ATTR_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, HAL_ATTR_READ,
               zcl_version);
    SETUP_ATTR(1, ZCL_ATTR_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, HAL_ATTR_READ,
               app_version);
    SETUP_ATTR(2, ZCL_ATTR_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, HAL_ATTR_READ,
               stack_version);
    SETUP_ATTR(3, ZCL_ATTR_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, HAL_ATTR_READ,
               hw_version);
    SETUP_ATTR(4, ZCL_ATTR_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, HAL_ATTR_READ,
               cluster->manuName);
    SETUP_ATTR(5, ZCL_ATTR_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, HAL_ATTR_READ,
               cluster->modelId);
    SETUP_ATTR(6, ZCL_ATTR_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, HAL_ATTR_READ,
               power_source);
    SETUP_ATTR(7, ZCL_ATTR_BASIC_DEV_ENABLED, ZCL_DATA_TYPE_BOOLEAN,
               HAL_ATTR_READ | HAL_ATTR_WRITE, cluster->deviceEnable);
    SETUP_ATTR(8, ZCL_ATTR_BASIC_SW_BUILD_ID, ZCL_DATA_TYPE_CHAR_STR, HAL_ATTR_READ,
               sw_build_id);
    SETUP_ATTR(9, ZCL_ATTR_BASIC_DATE_CODE, ZCL_DATA_TYPE_CHAR_STR, HAL_ATTR_READ,
               ZB_BUILD_DATE_YYYYMMDD);
    SETUP_ATTR(10, ZCL_ATTR_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster_revision);
    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id      = ZCL_CLUSTER_BASIC,
        .role            = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count =                     11,
        .attributes      = cluster->attr_infos,
    };
    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity,
                                    &endpoint_cluster);
}
