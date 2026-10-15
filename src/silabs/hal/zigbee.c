#include "hal/zigbee.h"

#include "app/framework/include/af.h"
#include "app/framework/plugin/ota-client/ota-client.h"
#include "network-steering.h"
#include <stddef.h>
#include <string.h>

#if ZCL_FIXED_ENDPOINT_COUNT < HAL_ZIGBEE_MAX_ENDPOINTS
#error "ZAP configuration has fewer endpoints than hal/zigbee_limits.h allows"
#endif

sl_zigbee_af_endpoint_type_t      endpoint_type_buffer[ZCL_FIXED_ENDPOINT_COUNT];
sl_zigbee_af_cluster_t            clusters_buffer[HAL_ZIGBEE_MAX_CLUSTERS];
sl_zigbee_af_attribute_metadata_t attributes_buffer[HAL_ZIGBEE_MAX_ATTRIBUTES];

hal_zigbee_endpoint *hal_endpoints;
uint8_t hal_endpoints_cnt;
static hal_zcl_activity_callback_t zcl_activity_callback = NULL;
static hal_zigbee_diagnostics_t    diagnostics;

static sl_zigbee_af_status_t attr_write_result_to_status(
    hal_zigbee_cmd_result_t result) {
    if (result == HAL_ZIGBEE_CMD_PROCESSED) return SL_ZIGBEE_ZCL_STATUS_SUCCESS;

    if (result == HAL_ZIGBEE_INVALID_VALUE) return SL_ZIGBEE_ZCL_STATUS_INVALID_VALUE;

    if (result == HAL_ZIGBEE_MALFORMED_COMMAND) return SL_ZIGBEE_ZCL_STATUS_MALFORMED_COMMAND;

    if (result == HAL_ZIGBEE_ACTION_DENIED) return SL_ZIGBEE_ZCL_STATUS_ACTION_DENIED;

    return SL_ZIGBEE_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
}

static void notify_zcl_activity(void) {
    if (zcl_activity_callback != NULL) {
        zcl_activity_callback();
    }
}

static hal_zigbee_cluster *find_unique_server_cluster(uint8_t endpoint,
                                                      uint16_t cluster_id) {
    hal_zigbee_cluster *result = NULL;

    for (uint8_t i = 0; i < hal_endpoints_cnt; i++) {
        if (hal_endpoints[i].endpoint != endpoint) continue;
        for (uint8_t j = 0; j < hal_endpoints[i].cluster_count; j++) {
            hal_zigbee_cluster *candidate = &hal_endpoints[i].clusters[j];

            if (candidate->cluster_id == cluster_id &&
                candidate->role == HAL_ZIGBEE_ROLE_SERVER) {
                if (result != NULL) return NULL;

                result = candidate;
            }
        }
    }
    return result;
}

static hal_zigbee_cluster *find_hal_cluster(uint8_t endpoint,
                                            sl_zigbee_af_cluster_id_t clusterId,
                                            hal_zigbee_cluster_role_t role,
                                            uint16_t manufacturer_code) {
    return hal_zigbee_find_cluster_exact(hal_endpoints, hal_endpoints_cnt,
                                         endpoint, clusterId, role,
                                         manufacturer_code);
}

static uint32_t on_command_callback(sl_service_opcode_t opcode,
                                    sl_service_function_context_t *context) {
    assert(opcode == SL_SERVICE_FUNCTION_TYPE_ZCL_COMMAND);

    sl_zigbee_af_cluster_command_t *cmd =
        (sl_zigbee_af_cluster_command_t *)context->data;
    uint16_t            manufacturer_code = cmd->mfgSpecific ? cmd->mfgCode : 0;
    hal_zigbee_cluster *hal_cluster       = find_hal_cluster(
        cmd->apsFrame->destinationEndpoint, cmd->apsFrame->clusterId,
        HAL_ZIGBEE_ROLE_SERVER, manufacturer_code);
    if (hal_cluster == NULL || hal_cluster->role != HAL_ZIGBEE_ROLE_SERVER ||
        hal_cluster->cmd_callback == NULL ||
        cmd->direction != ZCL_DIRECTION_CLIENT_TO_SERVER)
        return SL_ZIGBEE_ZCL_STATUS_UNSUP_COMMAND;

    uint8_t *payload     = NULL;
    uint16_t payload_len = 0;
    if (cmd->payloadStartIndex > cmd->bufLen) {
        return SL_ZIGBEE_ZCL_STATUS_MALFORMED_COMMAND;
    }
    if (cmd->bufLen > cmd->payloadStartIndex) {
        payload     = cmd->buffer + cmd->payloadStartIndex;
        payload_len = cmd->bufLen - cmd->payloadStartIndex;
    }

    hal_zigbee_incoming_command_t incoming = {
        .source_endpoint       = cmd->apsFrame->sourceEndpoint,
        .destination_endpoint  = cmd->apsFrame->destinationEndpoint,
        .source_address        = cmd->source,
        .cluster_id            = cmd->apsFrame->clusterId,
        .command_id            = cmd->commandId,
        .cluster_specific      = cmd->clusterSpecific,
        .manufacturer_specific = cmd->mfgSpecific,
        .manufacturer_code     = manufacturer_code,
        .direction             = cmd->direction == ZCL_DIRECTION_CLIENT_TO_SERVER ?
                                 HAL_ZIGBEE_DIR_CLIENT_TO_SERVER :
                                 HAL_ZIGBEE_DIR_SERVER_TO_CLIENT,
        .payload     = payload,
        .payload_len = payload_len,
    };
    hal_zigbee_cmd_result_t       res = hal_cluster->cmd_callback(&incoming);
    if (res == HAL_ZIGBEE_CMD_SKIPPED) {
        return SL_ZIGBEE_ZCL_STATUS_UNSUP_COMMAND;
    }
    sl_zigbee_af_status_t status = SL_ZIGBEE_ZCL_STATUS_SUCCESS;
    if (res == HAL_ZIGBEE_INVALID_VALUE) {
        status = SL_ZIGBEE_ZCL_STATUS_INVALID_VALUE;
    } else if (res == HAL_ZIGBEE_MALFORMED_COMMAND) {
        status = SL_ZIGBEE_ZCL_STATUS_MALFORMED_COMMAND;
    } else if (res == HAL_ZIGBEE_ACTION_DENIED) {
        status = SL_ZIGBEE_ZCL_STATUS_ACTION_DENIED;
    }
    sl_zigbee_af_send_immediate_default_response(status);
    return status;
}

bool sl_zigbee_af_pre_command_received_cb(sl_zigbee_af_cluster_command_t *cmd) {
    (void)cmd;
    notify_zcl_activity();
    return false;
}

bool hal_zigbee_init(hal_zigbee_endpoint *endpoints, uint8_t endpoints_cnt) {
    hal_zigbee_descriptor_error_t descriptor_error =
        hal_zigbee_validate_platform_graph(endpoints, endpoints_cnt);

    if (descriptor_error != HAL_ZIGBEE_DESCRIPTOR_OK) {
        diagnostics.descriptor_validation_failures++;
        diagnostics.last_descriptor_error = descriptor_error;
        return false;
    }

    hal_endpoints     = endpoints;
    hal_endpoints_cnt = endpoints_cnt;

    for (int i = 0; i < ZCL_FIXED_ENDPOINT_COUNT; i++) {
        sl_zigbee_af_endpoint_enable_disable(sli_zigbee_af_endpoints[i].endpoint,
                                             false);
    }

    sl_zigbee_af_endpoint_type_t *     endpoint_type_ptr = endpoint_type_buffer;
    sl_zigbee_af_cluster_t *           cluster_ptr       = clusters_buffer;
    sl_zigbee_af_attribute_metadata_t *attr_ptr          = attributes_buffer;

    for (int i = 0; i < endpoints_cnt; i++) {
        sli_zigbee_af_endpoints[i].endpoint      = endpoints[i].endpoint;
        sli_zigbee_af_endpoints[i].profileId     = endpoints[i].profile_id;
        sli_zigbee_af_endpoints[i].deviceId      = endpoints[i].device_id;
        sli_zigbee_af_endpoints[i].deviceVersion = endpoints[i].device_version;
        sli_zigbee_af_endpoints[i].endpointType  = endpoint_type_ptr;

        endpoint_type_ptr->clusterCount = endpoints[i].cluster_count;
        endpoint_type_ptr->cluster      = cluster_ptr;
        endpoint_type_ptr->endpointSize = 0;

        hal_zigbee_cluster *clusters = endpoints[i].clusters;

        for (int j = 0; j < endpoints[i].cluster_count; j++) {
            cluster_ptr->clusterId      = clusters[j].cluster_id;
            cluster_ptr->clusterSize    = 0;
            cluster_ptr->attributeCount = clusters[j].attribute_count;
            cluster_ptr->mask           =
                clusters[j].role ==
                HAL_ZIGBEE_ROLE_SERVER ? CLUSTER_MASK_SERVER : CLUSTER_MASK_CLIENT;
            cluster_ptr->attributes = attr_ptr;

            hal_zigbee_attribute *attributes = clusters[j].attributes;

            for (int l = 0; l < clusters[j].attribute_count; l++) {
                attr_ptr->attributeId   = attributes[l].attribute_id;
                attr_ptr->attributeType = attributes[l].data_type_id;
                attr_ptr->size          = attributes[l].size;
                attr_ptr->mask          = ATTRIBUTE_MASK_EXTERNAL_STORAGE;
                if (attributes[l].flag & HAL_ATTR_WRITE) {
                    attr_ptr->mask |= ATTRIBUTE_MASK_WRITABLE;
                }
                attr_ptr++;
            }

            if (clusters[j].cmd_callback) {
                sl_zigbee_subscribe_to_zcl_commands(
                    clusters[j].cluster_id,
                    clusters[j].manufacturer_code != 0 ?
                    clusters[j].manufacturer_code : 0xFFFF,
                    clusters[j].role == HAL_ZIGBEE_ROLE_SERVER ? ZCL_DIRECTION_CLIENT_TO_SERVER
                                  : ZCL_DIRECTION_SERVER_TO_CLIENT,
                    on_command_callback);
            }
            cluster_ptr++;
        }

        sl_zigbee_af_endpoint_enable_disable(sli_zigbee_af_endpoints[i].endpoint,
                                             true);
        endpoint_type_ptr++;
    }
    return true;
}

hal_zigbee_diagnostics_t hal_zigbee_get_diagnostics(void) {
    return diagnostics;
}

void hal_zigbee_notify_attribute_changed(uint8_t endpoint, uint16_t cluster_id,
                                         uint16_t attribute_id) {
    hal_zigbee_cluster *  cluster = find_unique_server_cluster(endpoint, cluster_id);
    hal_zigbee_attribute *attr    = hal_zigbee_find_attribute_in_cluster(
        cluster, attribute_id);

    if (cluster == NULL || cluster->role != HAL_ZIGBEE_ROLE_SERVER || attr == NULL ||
        (attr->flag & HAL_ATTR_REPORT) == 0) {
        return;
    }
    sl_zigbee_af_reporting_attribute_change_cb(
        endpoint, cluster_id, attribute_id,
        cluster->role == HAL_ZIGBEE_ROLE_SERVER ? CLUSTER_MASK_SERVER : CLUSTER_MASK_CLIENT, 0,
        attr->data_type_id, attr->value);
}

void hal_zigbee_register_on_zcl_activity_callback(
    hal_zcl_activity_callback_t callback) {
    zcl_activity_callback = callback;
}

sl_zigbee_af_status_t sl_zigbee_af_external_attribute_read_cb(
    uint8_t endpoint, sl_zigbee_af_cluster_id_t clusterId,
    sl_zigbee_af_attribute_metadata_t *attributeMetadata,
    uint16_t manufacturerCode, uint8_t *buffer, uint16_t maxReadLength) {
    hal_zigbee_cluster *cluster = find_hal_cluster(
        endpoint, clusterId, HAL_ZIGBEE_ROLE_SERVER,
        manufacturerCode == 0xFFFF ? 0 : manufacturerCode);

    if (cluster == NULL || cluster->role != HAL_ZIGBEE_ROLE_SERVER ||
        cluster->manufacturer_code !=
        (manufacturerCode == 0xFFFF ? 0 : manufacturerCode)) {
        return SL_ZIGBEE_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
    }
    hal_zigbee_attribute *attr = hal_zigbee_find_attribute_in_cluster(
        cluster, attributeMetadata->attributeId);

    if (attr == NULL || (attr->flag & HAL_ATTR_READ) == 0) {
        return SL_ZIGBEE_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
    }
    if (maxReadLength < attr->size) {
        return SL_ZIGBEE_ZCL_STATUS_INSUFFICIENT_SPACE;
    }

    memmove(buffer, attr->value, attr->size);

    return SL_ZIGBEE_ZCL_STATUS_SUCCESS;
}

sl_zigbee_af_status_t sl_zigbee_af_external_attribute_write_cb(
    uint8_t endpoint, sl_zigbee_af_cluster_id_t clusterId,
    sl_zigbee_af_attribute_metadata_t *attributeMetadata,
    uint16_t manufacturerCode, uint8_t *buffer) {
    hal_zigbee_cluster *cluster = find_hal_cluster(
        endpoint, clusterId, HAL_ZIGBEE_ROLE_SERVER,
        manufacturerCode == 0xFFFF ? 0 : manufacturerCode);

    if (cluster == NULL || cluster->role != HAL_ZIGBEE_ROLE_SERVER ||
        cluster->manufacturer_code !=
        (manufacturerCode == 0xFFFF ? 0 : manufacturerCode)) {
        return SL_ZIGBEE_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
    }
    hal_zigbee_attribute *attr = hal_zigbee_find_attribute_in_cluster(
        cluster, attributeMetadata->attributeId);

    if (attr == NULL || (attr->flag & HAL_ATTR_WRITE) == 0) {
        return SL_ZIGBEE_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
    }

    /* The framework hands over the value in the attribute's storage size;
     * strings carry their own length prefix. */
    uint16_t candidate_len = hal_zigbee_attr_encoded_size(attr->data_type_id,
                                                          buffer, attr->size);

    if (candidate_len == 0) return SL_ZIGBEE_ZCL_STATUS_INVALID_VALUE;

    /* HAL write contract (hal/zigbee.h): VALIDATE, then COMMIT, which stores
     * the value; only a cluster without a callback gets the copy. */
    if (cluster->attr_write_callback == NULL) {
        memmove(attr->value, buffer, candidate_len);
        return SL_ZIGBEE_ZCL_STATUS_SUCCESS;
    }
    sl_zigbee_af_status_t status = attr_write_result_to_status(
        cluster->attr_write_callback(endpoint, attr->attribute_id, buffer,
                                     candidate_len, HAL_ZIGBEE_ATTR_VALIDATE));

    if (status != SL_ZIGBEE_ZCL_STATUS_SUCCESS) return status;

    return attr_write_result_to_status(
        cluster->attr_write_callback(endpoint, attr->attribute_id, buffer,
                                     candidate_len, HAL_ZIGBEE_ATTR_COMMIT));
}

static bool network_steering_in_progress = false;

hal_zigbee_network_status_t hal_zigbee_get_network_status() {
    sl_zigbee_network_status_t ns = sl_zigbee_af_network_state();

    if (ns == SL_ZIGBEE_JOINED_NETWORK) {
        return HAL_ZIGBEE_NETWORK_JOINED;
    } else if (ns == SL_ZIGBEE_JOINING_NETWORK || network_steering_in_progress) {
        return HAL_ZIGBEE_NETWORK_JOINING;
    } else {
        return HAL_ZIGBEE_NETWORK_NOT_JOINED;
    }
}

static hal_network_status_change_callback_t network_status_change_callback =
    NULL;

static void notify_network_status_change(void) {
    if (network_status_change_callback != NULL) {
        network_status_change_callback(hal_zigbee_get_network_status());
    }
}

void hal_register_on_network_status_change_callback(
    hal_network_status_change_callback_t callback) {
    network_status_change_callback = callback;
}

void sl_zigbee_af_stack_status_cb(sl_status_t status) {
    (void)status;
    notify_network_status_change();
}

void hal_zigbee_leave_network() {
    sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
}

void hal_zigbee_start_network_steering() {
    if (!network_steering_in_progress) {
        network_steering_in_progress = true;
        sl_zigbee_af_network_steering_start();
    }
}

// Network steering complete callback
void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t totalBeacons,
                                               uint8_t joinAttempts,
                                               uint8_t finalState) {
    (void)status;
    (void)totalBeacons;
    (void)joinAttempts;
    (void)finalState;
    network_steering_in_progress = false;

    notify_network_status_change();
}

static uint8_t make_frame_control(const hal_zigbee_cmd *c) {
    uint8_t fc = 0;

    if (c->cluster_specific)
        fc |= ZCL_CLUSTER_SPECIFIC_COMMAND;
    if (c->direction == HAL_ZIGBEE_DIR_SERVER_TO_CLIENT)
        fc |= ZCL_FRAME_CONTROL_SERVER_TO_CLIENT;
    if (c->disable_default_rsp)
        fc |= ZCL_DISABLE_DEFAULT_RESPONSE_MASK;
    if (c->manufacturer_code)
        fc |= ZCL_MANUFACTURER_SPECIFIC_MASK;
    return fc;
}

static void fill_cmd(const hal_zigbee_cmd *c) {
    sl_zigbee_set_manufacturer_code(c->manufacturer_code);
    uint8_t fc = make_frame_control(c);

    if (c->payload_len == 0 || c->payload == NULL) {
        sl_zigbee_af_fill_external_buffer(fc, c->cluster_id, c->command_id, "");
    } else {
        sl_zigbee_af_fill_external_buffer(fc, c->cluster_id, c->command_id, "b",
                                          c->payload, c->payload_len);
    }
}

hal_zigbee_status_t hal_zigbee_send_cmd_to_bindings(const hal_zigbee_cmd *cmd) {
    if (!cmd)
        return HAL_ZIGBEE_ERR_BAD_ARG;

    if (sl_zigbee_af_network_state() != SL_ZIGBEE_JOINED_NETWORK)
        return HAL_ZIGBEE_ERR_NOT_JOINED;

    fill_cmd(cmd);

    sl_zigbee_af_set_command_endpoints(cmd->endpoint, cmd->endpoint);
    sl_status_t st_unicast   = sl_zigbee_af_send_command_unicast_to_bindings();
    sl_status_t st_multicast = sl_zigbee_af_send_command_multicast_to_bindings();

    return (st_unicast == SL_STATUS_OK || st_multicast == SL_STATUS_OK)
           ? HAL_ZIGBEE_OK : HAL_ZIGBEE_ERR_SEND_FAILED;
}

hal_zigbee_status_t
hal_zigbee_send_cmd_to_coordinator(const hal_zigbee_cmd *cmd) {
    sl_status_t status;

    if (cmd == NULL || (cmd->payload_len != 0 && cmd->payload == NULL)) {
        return HAL_ZIGBEE_ERR_BAD_ARG;
    }
    if (sl_zigbee_af_network_state() != SL_ZIGBEE_JOINED_NETWORK) {
        return HAL_ZIGBEE_ERR_NOT_JOINED;
    }
    fill_cmd(cmd);
    sl_zigbee_af_set_command_endpoints(cmd->endpoint, 1);
    status = sl_zigbee_af_send_command_unicast(SL_ZIGBEE_OUTGOING_DIRECT,
                                               0x0000);
    return status == SL_STATUS_OK ? HAL_ZIGBEE_OK
                                  : HAL_ZIGBEE_ERR_SEND_FAILED;
}

hal_zigbee_status_t
hal_zigbee_send_report_attr(uint8_t endpoint, uint16_t cluster_id,
                            uint16_t attr_id, uint8_t zcl_type_id,
                            const void *value, uint8_t value_len) {
    hal_zigbee_cluster *  cluster   = find_unique_server_cluster(endpoint, cluster_id);
    hal_zigbee_attribute *attribute = hal_zigbee_find_attribute_in_cluster(
        cluster, attr_id);

    if (sl_zigbee_af_network_state() != SL_ZIGBEE_JOINED_NETWORK)
        return HAL_ZIGBEE_ERR_NOT_JOINED;

    uint8_t buf[2 + 1 + 8]; /* attrId(2) + type(1) + value */
    if (value == NULL || attribute == NULL ||
        (attribute->flag & HAL_ATTR_REPORT) == 0 ||
        attribute->data_type_id != zcl_type_id || attribute->size != value_len ||
        value_len > 8)
        return HAL_ZIGBEE_ERR_BAD_ARG;

    buf[0] = (uint8_t)(attr_id & 0xFF);
    buf[1] = (uint8_t)(attr_id >> 8);
    buf[2] = zcl_type_id;
    if (value_len)
        memmove(&buf[3], value, value_len);

    sl_status_t st;

    if (cluster->manufacturer_code != 0) {
        sl_zigbee_set_manufacturer_code(cluster->manufacturer_code);
        st = sl_zigbee_af_fill_external_buffer(
            ZCL_FRAME_CONTROL_SERVER_TO_CLIENT | ZCL_MANUFACTURER_SPECIFIC_MASK,
            cluster_id, ZCL_REPORT_ATTRIBUTES_COMMAND_ID, "b", buf,
            3 + value_len);
    } else {
        st = sl_zigbee_af_fill_command_global_server_to_client_report_attributes(
            cluster_id, buf, 3 + value_len);
    }
    if (st != SL_STATUS_OK)
        return HAL_ZIGBEE_ERR_SEND_FAILED;

    sl_zigbee_af_set_command_endpoints(endpoint, endpoint);
    st = sl_zigbee_af_send_command_unicast_to_bindings();
    return (st == SL_STATUS_OK) ? HAL_ZIGBEE_OK : HAL_ZIGBEE_ERR_SEND_FAILED;
}

hal_zigbee_status_t hal_zigbee_send_announce(void) {
    if (sl_zigbee_send_device_announcement() != SL_STATUS_OK) {
        return HAL_ZIGBEE_ERR_SEND_FAILED;
    }
    return HAL_ZIGBEE_OK;
}

static bool hal_zigbee_poll_rate_supported(void) {
    sl_zigbee_node_type_t node_type = SL_ZIGBEE_UNKNOWN_DEVICE;

    if (sl_zigbee_af_get_node_type(&node_type) != SL_STATUS_OK) {
        return false;
    }

    return node_type == SL_ZIGBEE_END_DEVICE ||
           node_type == SL_ZIGBEE_SLEEPY_END_DEVICE ||
           node_type == SL_ZIGBEE_S2S_INITIATOR_DEVICE ||
           node_type == SL_ZIGBEE_S2S_TARGET_DEVICE;
}

void hal_zigbee_set_poll_rate_ms(uint32_t poll_rate_ms) {
    if (!hal_zigbee_poll_rate_supported()) {
        return;
    }
    // Only set the long poll interval, keep short poll managed by
    // SDK itself for Silabs
    sl_zigbee_af_set_long_poll_interval_ms_cb(poll_rate_ms);
}

uint32_t hal_zigbee_get_poll_rate_ms(void) {
    if (!hal_zigbee_poll_rate_supported()) {
        return 0;
    }

    return sl_zigbee_af_get_long_poll_interval_ms_cb();
}

void hal_zigbee_init_ota() {
}
