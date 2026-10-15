#include "device_config_cluster.h"

#include "cluster_common.h"
#include "zcl_types.h"
#include "device_config/device_config_service.h"
#include "management_protocol.h"
#include <string.h>

static zigbee_device_config_cluster_t *instance;

static void sync_cluster(void) {
    const device_config_service_state_t *state =
        device_config_service_get_state();

    if (instance == NULL) return;

    instance->schema_version = state->schema_version;
    instance->revision       = state->revision;
    instance->config.length  = state->config_len;
    memcpy(instance->config.data, state->config, state->config_len);
    instance->config.data[state->config_len] = '\0';
    instance->status            = state->status;
    instance->error             = state->error;
    instance->led_mode          = state->led_mode;
    instance->multi_press_reset = state->multi_press_reset;
    instance->child_lock        = state->child_lock;
}

/* Converters read the lock instead of configuring a report for it (the
 * Telink reporting table is full on 4-gang boards); the change is still
 * flagged for a report configured by hand. */
static void service_changed(void) {
    sync_cluster();
    if (instance != NULL) {
        hal_zigbee_notify_attribute_changed(instance->endpoint, ZCL_CLUSTER_DEVICE_CONFIG,
                                            ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK);
    }
}

static hal_zigbee_cmd_result_t attr_write(uint8_t endpoint,
                                          uint16_t attribute_id,
                                          const uint8_t *candidate,
                                          uint16_t candidate_len,
                                          hal_zigbee_attr_write_phase_t phase) {
    (void)endpoint;
    if (instance == NULL) return HAL_ZIGBEE_CMD_SKIPPED;

    device_config_error_t result = DEVICE_CONFIG_ERROR_ARGUMENT;
    if (attribute_id == ZCL_ATTR_DEVICE_CONFIG_TEXT) {
        uint16_t config_len;

        if (candidate == NULL || candidate_len < 2 ||
            candidate_len > sizeof(instance->config)) {
            return HAL_ZIGBEE_INVALID_VALUE;
        }
        // The HALs reject the invalid long-string length 0xFFFF; any length
        // that does not fit the text buffer, including it, is refused here.
        config_len = (uint16_t)candidate[0] | ((uint16_t)candidate[1] << 8);
        if (config_len >= sizeof(instance->config.data) ||
            (candidate_len != sizeof(instance->config) &&
             candidate_len != config_len + 2)) return HAL_ZIGBEE_INVALID_VALUE;

        if (phase == HAL_ZIGBEE_ATTR_VALIDATE) {
            return device_config_service_preflight((const char *)&candidate[2],
                                                   config_len) == CONFIG_PARSE_OK ?
                   HAL_ZIGBEE_CMD_PROCESSED : HAL_ZIGBEE_INVALID_VALUE;
        }
        result = device_config_service_apply((const char *)&candidate[2],
                                             config_len);
    } else if (attribute_id == ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE) {
        if (candidate == NULL || candidate_len != sizeof(instance->led_mode) ||
            candidate[0] > DEVICE_CONFIG_LED_ON) return HAL_ZIGBEE_INVALID_VALUE;

        if (phase == HAL_ZIGBEE_ATTR_VALIDATE) return HAL_ZIGBEE_CMD_PROCESSED;

        result = device_config_service_set_led_mode(candidate[0]);
    } else if (attribute_id == ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET) {
        if (candidate == NULL || candidate_len != sizeof(instance->multi_press_reset))
            return HAL_ZIGBEE_INVALID_VALUE;

        if (phase == HAL_ZIGBEE_ATTR_VALIDATE) return HAL_ZIGBEE_CMD_PROCESSED;

        result = device_config_service_set_multi_press_reset(candidate[0]);
    } else if (attribute_id == ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK) {
        if (candidate == NULL || candidate_len != sizeof(instance->child_lock) ||
            candidate[0] > 1) return HAL_ZIGBEE_INVALID_VALUE;

        if (phase == HAL_ZIGBEE_ATTR_VALIDATE) return HAL_ZIGBEE_CMD_PROCESSED;

        result = device_config_service_set_child_lock(candidate[0]);
    } else {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    sync_cluster();
    return result == DEVICE_CONFIG_ERROR_NONE ? HAL_ZIGBEE_CMD_PROCESSED :
           HAL_ZIGBEE_INVALID_VALUE;
}

void device_config_cluster_init(zigbee_device_config_cluster_t *cluster) {
    memset(cluster, 0, sizeof(*cluster));
    instance = cluster;
    sync_cluster();
}

void device_config_cluster_add_to_endpoint(zigbee_device_config_cluster_t *cluster,
                                           hal_zigbee_endpoint *endpoint) {
    if (!hal_zigbee_endpoint_reserve_clusters(endpoint,
                                              endpoint->cluster_capacity, 1)) {
        return;
    }
    instance          = cluster;
    cluster->endpoint = endpoint->endpoint;
    sync_cluster();
    device_config_service_set_change_listener(service_changed);
    SETUP_ATTR(0, ZCL_ATTR_DEVICE_CONFIG_SCHEMA_VERSION, ZCL_DATA_TYPE_UINT16,
               HAL_ATTR_READ, cluster->schema_version);
    SETUP_ATTR(1, ZCL_ATTR_DEVICE_CONFIG_REVISION, ZCL_DATA_TYPE_UINT32,
               HAL_ATTR_READ, cluster->revision);
    SETUP_ATTR(2, ZCL_ATTR_DEVICE_CONFIG_TEXT, ZCL_DATA_TYPE_LONG_CHAR_STR,
               HAL_ATTR_READ | HAL_ATTR_WRITE, cluster->config);
    SETUP_ATTR(3, ZCL_ATTR_DEVICE_CONFIG_STATUS, ZCL_DATA_TYPE_ENUM8,
               HAL_ATTR_READ | HAL_ATTR_REPORT, cluster->status);
    SETUP_ATTR(4, ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR, ZCL_DATA_TYPE_ENUM8,
               HAL_ATTR_READ | HAL_ATTR_REPORT, cluster->error);
    SETUP_ATTR(5, ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE, ZCL_DATA_TYPE_ENUM8,
               HAL_ATTR_READ | HAL_ATTR_WRITE, cluster->led_mode);
    SETUP_ATTR(6, ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET, ZCL_DATA_TYPE_UINT8,
               HAL_ATTR_READ | HAL_ATTR_WRITE, cluster->multi_press_reset);
    SETUP_ATTR(7, ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK, ZCL_DATA_TYPE_BOOLEAN,
               HAL_ATTR_READ | HAL_ATTR_WRITE | HAL_ATTR_REPORT, cluster->child_lock);
    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id        = ZCL_CLUSTER_DEVICE_CONFIG,
        .manufacturer_code = ZIGBEE_MANAGEMENT_MANUFACTURER_CODE,
        .role                = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count     =                                   8,
        .attributes          = cluster->attr_infos,
        .attr_write_callback = attr_write,
    };
    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity,
                                    &endpoint_cluster);
}
