#pragma pack(push, 1)
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "zcl_multistate_input.h"
#include "zcl_onoff_configuration.h"
#pragma pack(pop)

#include "telink_size_t_hack.h"

#include "hal/zigbee.h"
#include "telink_zigbee_hal.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/zcl_ids.h"
#include "zigbee/zcl_frame_check.h"

#if ZCL_CLUSTER_NUM_MAX != HAL_ZIGBEE_MAX_ZCL_REGISTRATIONS
#error "ZCL_CLUSTER_NUM_MAX must come from hal/zigbee_limits.h"
#endif

// Storage for Telink endpoint configuration
static af_simple_descriptor_t endpoint_descriptors[HAL_ZIGBEE_MAX_ENDPOINTS];
static u16 in_clusters[HAL_ZIGBEE_MAX_SERVER_CLUSTERS];
static u16 out_clusters[HAL_ZIGBEE_MAX_CLIENT_CLUSTERS];
static zcl_specClusterInfo_t cluster_infos[HAL_ZIGBEE_MAX_CLUSTERS];
static zclAttrInfo_t         attr_tables[HAL_ZIGBEE_MAX_ATTRIBUTES];
static hal_zigbee_cluster *  attr_table_clusters[HAL_ZIGBEE_MAX_ATTRIBUTES];

// HAL state tracking
static hal_zigbee_endpoint *hal_endpoints = NULL;
static uint8_t hal_endpoints_cnt          = 0;
static hal_zcl_activity_callback_t zcl_activity_callback = NULL;
static hal_zigbee_diagnostics_t    diagnostics;
static cluster_registerFunc_t get_register_func_by_cluster_id(u16 cluster_id);

static status_t attr_write_result_to_status(hal_zigbee_cmd_result_t result) {
    if (result == HAL_ZIGBEE_CMD_PROCESSED) return ZCL_STA_SUCCESS;

    if (result == HAL_ZIGBEE_INVALID_VALUE) return ZCL_STA_INVALID_VALUE;

    if (result == HAL_ZIGBEE_MALFORMED_COMMAND) return ZCL_STA_MALFORMED_COMMAND;

    if (result == HAL_ZIGBEE_ACTION_DENIED) return ZCL_STA_ACTION_DENIED;

    return ZCL_STA_UNSUPPORTED_ATTRIBUTE;
}

static status_t register_private_cluster(u8 endpoint, u16 unused_manu_code,
                                         u8 attr_num,
                                         const zclAttrInfo_t attr_table[],
                                         cluster_forAppCb_t callback) {
    (void)unused_manu_code;
    if (attr_table < attr_tables || attr_table >= attr_tables + HAL_ZIGBEE_MAX_ATTRIBUTES)
        return ZCL_STA_FAILURE;

    hal_zigbee_cluster *cluster = attr_table_clusters[attr_table - attr_tables];
    if (cluster != NULL && cluster->manufacturer_code != 0) {
        return zcl_registerCluster(endpoint, cluster->cluster_id,
                                   cluster->manufacturer_code, attr_num,
                                   attr_table, NULL, callback);
    }
    return ZCL_STA_FAILURE;
}

static void record_descriptor_error(hal_zigbee_descriptor_error_t error) {
    diagnostics.descriptor_validation_failures++;
    diagnostics.last_descriptor_error = error;
    printf("Zigbee descriptor graph rejected: %d\r\n", error);
}

bool telink_zigbee_hal_zcl_graph_supported(hal_zigbee_endpoint *endpoints,
                                           uint8_t endpoints_cnt) {
    hal_zigbee_descriptor_error_t error =
        hal_zigbee_validate_platform_graph(endpoints, endpoints_cnt);

    if (error != HAL_ZIGBEE_DESCRIPTOR_OK) {
        record_descriptor_error(error);
        return false;
    }
    for (uint8_t i = 0; i < endpoints_cnt; i++) {
        for (uint8_t j = 0; j < endpoints[i].cluster_count; j++) {
            hal_zigbee_cluster *cluster = &endpoints[i].clusters[j];

            if (cluster->manufacturer_code == 0 &&
                cluster->cluster_id != ZCL_CLUSTER_OTA &&
                get_register_func_by_cluster_id(cluster->cluster_id) == NULL) {
                record_descriptor_error(HAL_ZIGBEE_DESCRIPTOR_UNREGISTERED_CLUSTER);
                return false;
            }
        }
    }
    return true;
}

static cluster_registerFunc_t get_register_func_by_cluster_id(u16 cluster_id) {
    if (cluster_id == ZCL_CLUSTER_GEN_BASIC) {
        return zcl_basic_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_POWER_CFG ||
        cluster_id == ZCL_CLUSTER_POWER_CFG) {
        return zcl_powerCfg_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_IDENTIFY) {
        return zcl_identify_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_GROUPS) {
        return zcl_group_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_ON_OFF_SWITCH_CONFIG) {
        return zcl_onoff_configuration_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_LEVEL_CONTROL) {
        return zcl_level_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_ON_OFF) {
        return zcl_onOff_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_MULTISTATE_INPUT_BASIC) {
        return zcl_multistate_input_register;
    }
    if (cluster_id == ZCL_CLUSTER_CLOSURES_WINDOW_COVERING) {
        return zcl_windowCovering_register;
    }
    if (cluster_id == ZCL_CLUSTER_GEN_POLL_CONTROL) {
        return zcl_pollCtrl_register;
    }
    return NULL;
}

static status_t cmd_callback(zclIncomingAddrInfo_t *addr_info, u8 cmd_id,
                             void *cmd_payload) {
    zclIncoming_t *message = (zclIncoming_t *)((char *)addr_info -
                                               offsetof(zclIncoming_t, addrInfo));
    hal_zigbee_incoming_command_t command = {
        .source_endpoint       = addr_info->srcEp,
        .destination_endpoint  = addr_info->dstEp,
        .source_address        = addr_info->srcAddr,
        .cluster_id            = message->msg->indInfo.cluster_id,
        .command_id            = cmd_id,
        .cluster_specific      =                                 1,
        .manufacturer_specific = message->hdr.frmCtrl.bf.manufSpec,
        .manufacturer_code     = message->hdr.manufCode,
        .direction             = message->hdr.frmCtrl.bf.dir ?
                                 HAL_ZIGBEE_DIR_SERVER_TO_CLIENT :
                                 HAL_ZIGBEE_DIR_CLIENT_TO_SERVER,
        .payload     = message->pData,
        .payload_len = message->dataLen,
    };
    hal_zigbee_cluster *          cluster = hal_zigbee_find_cluster_exact(
        hal_endpoints, hal_endpoints_cnt, command.destination_endpoint,
        command.cluster_id, HAL_ZIGBEE_ROLE_SERVER, command.manufacturer_code);

    if (cluster && cluster->cmd_callback &&
        command.direction == HAL_ZIGBEE_DIR_CLIENT_TO_SERVER &&
        command.manufacturer_specific == (cluster->manufacturer_code != 0)) {
        hal_zigbee_cmd_result_t status = cluster->cmd_callback(&command);
        if (status == HAL_ZIGBEE_CMD_PROCESSED) {
            return ZCL_STA_SUCCESS;
        } else if (status == HAL_ZIGBEE_INVALID_VALUE) {
            return ZCL_STA_INVALID_VALUE;
        } else if (status == HAL_ZIGBEE_MALFORMED_COMMAND) {
            return ZCL_STA_MALFORMED_COMMAND;
        } else if (status == HAL_ZIGBEE_ACTION_DENIED) {
            return ZCL_STA_ACTION_DENIED;
        } else if (status == HAL_ZIGBEE_CMD_SKIPPED) {
            return ZCL_STA_UNSUP_CLUSTER_COMMAND;
        }
    }
    return(ZCL_STA_SUCCESS);
}

/* Called by the patched zcl_foundationCmdHandler() before any parser runs;
 * see zigbee/zcl_frame_check.h. */
bool hal_zigbee_telink_foundation_frame_ok(zclIncoming_t *message) {
    // zcl_parseInCfgReportCmd() and zcl_readReportCfgHandler() size their allocations in a u8.
    static const zcl_frame_limits_t limits = {
        .config_report_records   =
            (255u - sizeof(zclCfgReportCmd_t)) / sizeof(zclCfgReportRec_t),
        .read_report_cfg_records =
            (255u - sizeof(zclReadReportCfgRspCmd_t)) / sizeof(zclReportCfgRspRec_t),
    };

    return zcl_frame_is_well_formed(message->hdr.cmd, message->pData,
                                    message->dataLen, &limits);
}

/*
 * The SDK frees the parsed form of a foundation command (attrCmd) only after
 * passing it to the ZCL hook, so a hook must be registered or every received
 * read, write or report leaks an event buffer.
 */
static void zcl_foundation_hook(zclIncoming_t *message) {
    (void)message;
}

/*
 * Called by the patched zcl_attrWrite() before its own checks and before
 * zcl_setAttrVal() copies the record into the attribute storage, which gives
 * the HAL write contract of hal/zigbee.h: VALIDATE, COMMIT, then the copy.
 * `enable` is false in the first pass of Write Attributes Undivided, which
 * only validates; the second pass writes each record with `enable` true.
 */
status_t hal_zigbee_telink_pre_write(zclIncoming_t *message, u8 endpoint,
                                     u16 cluster_id, zclWriteRec_t *record,
                                     bool enable) {
    hal_zigbee_cluster *  cluster;
    hal_zigbee_attribute *attribute;
    uint16_t candidate_len;

    if (message == NULL || record == NULL) {
        return ZCL_STA_UNSUPPORTED_ATTRIBUTE;
    }
    if (endpoint != message->msg->indInfo.dst_ep ||
        cluster_id != message->msg->indInfo.cluster_id ||
        message->hdr.frmCtrl.bf.dir != ZCL_FRAME_CLIENT_SERVER_DIR) {
        return ZCL_STA_UNSUPPORTED_ATTRIBUTE;
    }
    cluster = hal_zigbee_find_cluster_exact(
        hal_endpoints, hal_endpoints_cnt, endpoint, cluster_id,
        HAL_ZIGBEE_ROLE_SERVER, message->hdr.manufCode);
    if (cluster == NULL || message->hdr.frmCtrl.bf.manufSpec !=
        (cluster->manufacturer_code != 0)) {
        return ZCL_STA_UNSUPPORTED_ATTRIBUTE;
    }
    attribute = hal_zigbee_find_attribute_in_cluster(cluster, record->attrID);
    if (attribute == NULL || (attribute->flag & HAL_ATTR_WRITE) == 0) {
        return ZCL_STA_UNSUPPORTED_ATTRIBUTE;
    }
    if (record->dataType != attribute->data_type_id) return ZCL_STA_INVALID_DATA_TYPE;

    // zcl_setAttrVal() copies zcl_getAttrSize() bytes into the attribute
    // storage; for strings that size is the prefix plus its length, computed
    // here without the SDK's 16-bit wrap.
    candidate_len = hal_zigbee_type_is_string(record->dataType) ?
                    hal_zigbee_attr_encoded_size(record->dataType, record->attrData,
                                                 attribute->size) :
                    zcl_getAttrSize(record->dataType, record->attrData);
    if (candidate_len == 0 || candidate_len > attribute->size) return ZCL_STA_INVALID_VALUE;

    if (cluster->attr_write_callback == NULL) return ZCL_STA_SUCCESS;

    hal_zigbee_cmd_result_t result = cluster->attr_write_callback(
        endpoint, attribute->attribute_id, record->attrData, candidate_len,
        HAL_ZIGBEE_ATTR_VALIDATE);
    if (result != HAL_ZIGBEE_CMD_PROCESSED) return attr_write_result_to_status(result);

    if (!enable) return ZCL_STA_SUCCESS;

    return attr_write_result_to_status(cluster->attr_write_callback(
                                           endpoint, attribute->attribute_id, record->attrData,
                                           candidate_len,
                                           HAL_ZIGBEE_ATTR_COMMIT));
}

static void af_rx_callback(void *arg) {
    if (zcl_activity_callback != NULL) {
        zcl_activity_callback();
    }
    zcl_rx_handler(arg);
}

static void unregister_endpoints(hal_zigbee_endpoint *endpoints,
                                 uint8_t endpoints_cnt) {
    for (uint8_t i = 0; i < endpoints_cnt; i++) {
        af_endpointUnregister(endpoints[i].endpoint);
    }
    zcl_init(zcl_foundation_hook); // empties the ZCL cluster table
    hal_endpoints     = NULL;
    hal_endpoints_cnt = 0;
}

bool telink_zigbee_hal_zcl_init(hal_zigbee_endpoint *endpoints,
                                uint8_t endpoints_cnt) {
    if (!telink_zigbee_hal_zcl_graph_supported(endpoints, endpoints_cnt)) {
        return false;
    }
    zcl_init(zcl_foundation_hook);
    zcl_reportingTabInit();

    memset(endpoint_descriptors, 0, sizeof(endpoint_descriptors));
    memset(attr_table_clusters, 0, sizeof(attr_table_clusters));
    hal_endpoints     = endpoints;
    hal_endpoints_cnt = endpoints_cnt;
    af_simple_descriptor_t *endpoint_desc_ptr = endpoint_descriptors;
    u16 *in_cluster_ptr  = in_clusters;
    u16 *out_cluster_ptr = out_clusters;
    zcl_specClusterInfo_t *cluster_info_ptr = cluster_infos;
    zclAttrInfo_t *        attr_table_ptr   = attr_tables;

    for (hal_zigbee_endpoint *endpoint = endpoints;
         endpoint < endpoints + hal_endpoints_cnt; endpoint++) {
        endpoint_desc_ptr->endpoint            = endpoint->endpoint;
        endpoint_desc_ptr->app_profile_id      = endpoint->profile_id;
        endpoint_desc_ptr->app_dev_id          = endpoint->device_id;
        endpoint_desc_ptr->app_dev_ver         = endpoint->device_version;
        endpoint_desc_ptr->app_in_cluster_lst  = in_cluster_ptr;
        endpoint_desc_ptr->app_out_cluster_lst = out_cluster_ptr;

        zcl_specClusterInfo_t *endpoint_first_cluster_ptr = cluster_info_ptr;
        for (hal_zigbee_cluster *cluster = endpoint->clusters;
             cluster < endpoint->clusters + endpoint->cluster_count; cluster++) {
            if (cluster->role == HAL_ZIGBEE_ROLE_SERVER) {
                *in_cluster_ptr = cluster->cluster_id;
                in_cluster_ptr++;
                endpoint_desc_ptr->app_in_cluster_count++;
            } else {
                *out_cluster_ptr = cluster->cluster_id;
                out_cluster_ptr++;
                endpoint_desc_ptr->app_out_cluster_count++;
            }
            if (cluster->cluster_id == ZCL_CLUSTER_OTA) {
                continue;
            }
            cluster_info_ptr->clusterId           = cluster->cluster_id;
            cluster_info_ptr->manuCode            = cluster->manufacturer_code;
            cluster_info_ptr->attrTbl             = attr_table_ptr;
            cluster_info_ptr->attrNum             = 0;
            cluster_info_ptr->clusterRegisterFunc = cluster->manufacturer_code != 0 ?
                                                    register_private_cluster :
                                                    get_register_func_by_cluster_id(
                cluster->cluster_id);
            cluster_info_ptr->clusterAppCb = cluster->cmd_callback != NULL ?
                                             cmd_callback : NULL;
            for (hal_zigbee_attribute *attr = cluster->attributes;
                 attr < cluster->attributes + cluster->attribute_count; attr++) {
                attr_table_clusters[attr_table_ptr - attr_tables] = cluster;
                attr_table_ptr->id     = attr->attribute_id;
                attr_table_ptr->type   = attr->data_type_id;
                attr_table_ptr->access = 0;
                if (attr->flag & HAL_ATTR_READ)
                    attr_table_ptr->access |= ACCESS_CONTROL_READ;
                if (attr->flag & HAL_ATTR_WRITE)
                    attr_table_ptr->access |= ACCESS_CONTROL_WRITE;
                if (attr->flag & HAL_ATTR_REPORT)
                    attr_table_ptr->access |= ACCESS_CONTROL_REPORTABLE;
                attr_table_ptr->data = attr->value;
                attr_table_ptr++;
                cluster_info_ptr->attrNum++;
            }

            cluster_info_ptr++;
        }
        if (!af_endpointRegister(endpoint->endpoint, endpoint_desc_ptr,
                                 af_rx_callback, NULL)) {
            record_descriptor_error(HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_COUNT);
            unregister_endpoints(endpoints, (uint8_t)(endpoint - endpoints));
            return false;
        }
        // zcl_register() ignores every refusal except a full table, so
        // register one by one and treat any refusal as a failed graph.
        for (zcl_specClusterInfo_t *info = endpoint_first_cluster_ptr;
             info < cluster_info_ptr; info++) {
            status_t status = info->clusterRegisterFunc(
                endpoint->endpoint, info->manuCode, info->attrNum, info->attrTbl,
                info->clusterAppCb);

            if (status != ZCL_STA_SUCCESS) {
                printf("zcl_registerCluster ep=%d cluster=0x%04x failed: %d\r\n",
                       endpoint->endpoint, info->clusterId, status);
                record_descriptor_error(HAL_ZIGBEE_DESCRIPTOR_REGISTRATION_CAPACITY);
                unregister_endpoints(endpoints, (uint8_t)(endpoint - endpoints + 1));
                return false;
            }
        }

        endpoint_desc_ptr++;
    }
    return true;
}

hal_zigbee_diagnostics_t hal_zigbee_get_diagnostics(void) {
    return diagnostics;
}

void hal_zigbee_notify_attribute_changed(uint8_t endpoint, uint16_t cluster_id,
                                         uint16_t attribute_id) {
    hal_zigbee_cluster *  cluster = NULL;
    hal_zigbee_attribute *attr    = NULL;

    for (uint8_t i = 0; i < hal_endpoints_cnt; i++) {
        if (hal_endpoints[i].endpoint != endpoint) continue;
        for (uint8_t j = 0; j < hal_endpoints[i].cluster_count; j++) {
            hal_zigbee_cluster *candidate = &hal_endpoints[i].clusters[j];

            if (candidate->cluster_id == cluster_id &&
                candidate->role == HAL_ZIGBEE_ROLE_SERVER) {
                if (cluster != NULL) return;

                cluster = candidate;
                attr    = hal_zigbee_find_attribute_in_cluster(cluster, attribute_id);
            }
        }
    }

    if (cluster == NULL || attr == NULL || (attr->flag & HAL_ATTR_REPORT) == 0)
        return;

    report_handler(); // Trigger reporting if needed
}

hal_zigbee_status_t hal_zigbee_send_cmd_to_bindings(const hal_zigbee_cmd *cmd) {
    epInfo_t dstEpInfo;

    TL_SETSTRUCTCONTENT(dstEpInfo, 0);

    dstEpInfo.profileId   = HA_PROFILE_ID;
    dstEpInfo.dstAddrMode = APS_DSTADDR_EP_NOTPRESETNT;
    zcl_sendCmd(cmd->endpoint, &dstEpInfo, cmd->cluster_id, cmd->command_id,
                cmd->cluster_specific,
                cmd->direction == HAL_ZIGBEE_DIR_CLIENT_TO_SERVER
                  ? ZCL_FRAME_CLIENT_SERVER_DIR
                  : ZCL_FRAME_SERVER_CLIENT_DIR,
                cmd->disable_default_rsp, cmd->manufacturer_code, ZCL_SEQ_NUM,
                cmd->payload_len, (u8 *)cmd->payload);

    return HAL_ZIGBEE_OK;
}

hal_zigbee_status_t
hal_zigbee_send_cmd_to_coordinator(const hal_zigbee_cmd *cmd) {
    epInfo_t dstEpInfo;

    if (cmd == NULL || (cmd->payload_len != 0 && cmd->payload == NULL)) {
        return HAL_ZIGBEE_ERR_BAD_ARG;
    }
    if (!zb_isDeviceJoinedNwk()) {
        return HAL_ZIGBEE_ERR_NOT_JOINED;
    }
    TL_SETSTRUCTCONTENT(dstEpInfo, 0);
    dstEpInfo.profileId         = cmd->profile_id;
    dstEpInfo.dstAddrMode       = APS_SHORT_DSTADDR_WITHEP;
    dstEpInfo.dstAddr.shortAddr = 0x0000;
    dstEpInfo.dstEp             = 1;
    zcl_sendCmd(cmd->endpoint, &dstEpInfo, cmd->cluster_id, cmd->command_id,
                cmd->cluster_specific,
                cmd->direction == HAL_ZIGBEE_DIR_CLIENT_TO_SERVER
                  ? ZCL_FRAME_CLIENT_SERVER_DIR
                  : ZCL_FRAME_SERVER_CLIENT_DIR,
                cmd->disable_default_rsp, cmd->manufacturer_code, ZCL_SEQ_NUM,
                cmd->payload_len, (u8 *)cmd->payload);
    return HAL_ZIGBEE_OK;
}

hal_zigbee_status_t
hal_zigbee_send_report_attr(uint8_t endpoint, uint16_t cluster_id,
                            uint16_t attr_id, uint8_t zcl_type_id,
                            const void *value, uint8_t value_len) {
    epInfo_t              dstEpInfo;
    hal_zigbee_cluster *  cluster   = NULL;
    hal_zigbee_attribute *attribute = NULL;

    for (uint8_t i = 0; i < hal_endpoints_cnt; i++) {
        if (hal_endpoints[i].endpoint != endpoint) continue;
        for (uint8_t j = 0; j < hal_endpoints[i].cluster_count; j++) {
            hal_zigbee_cluster *candidate = &hal_endpoints[i].clusters[j];

            if (candidate->cluster_id == cluster_id &&
                candidate->role == HAL_ZIGBEE_ROLE_SERVER) {
                if (cluster != NULL) return HAL_ZIGBEE_ERR_BAD_ARG;

                cluster = candidate;
            }
        }
    }
    attribute = hal_zigbee_find_attribute_in_cluster(cluster, attr_id);
    if (value == NULL || attribute == NULL ||
        (attribute->flag & HAL_ATTR_REPORT) == 0 ||
        attribute->data_type_id != zcl_type_id || attribute->size != value_len) {
        return HAL_ZIGBEE_ERR_BAD_ARG;
    }
    if (!zb_isDeviceJoinedNwk()) {
        return HAL_ZIGBEE_ERR_NOT_JOINED;
    }
    TL_SETSTRUCTCONTENT(dstEpInfo, 0);
    dstEpInfo.profileId   = HA_PROFILE_ID;
    dstEpInfo.dstAddrMode = APS_DSTADDR_EP_NOTPRESETNT;
    zcl_sendReportCmd(endpoint, &dstEpInfo, TRUE, ZCL_FRAME_SERVER_CLIENT_DIR,
                      cluster_id, attr_id, zcl_type_id, (u8 *)value);
    return HAL_ZIGBEE_OK;
}

void hal_zigbee_register_on_zcl_activity_callback(hal_zcl_activity_callback_t callback) {
    zcl_activity_callback = callback;
}

// Internal interface functions

af_simple_descriptor_t *telink_zigbee_hal_zcl_get_descriptors(void) {
    return endpoint_descriptors;
}
