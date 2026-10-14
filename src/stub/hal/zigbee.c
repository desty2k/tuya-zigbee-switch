#include "hal/zigbee.h"
#include "stub/hal/stub.h"
#include "stub/machine_io.h"
#include "zigbee/zcl_frame_check.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bytes_to_hexstr(const uint8_t *bytes, size_t len, char *out) {
    static const char HEX_DIGITS[] = "0123456789ABCDEF";

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = bytes[i];
        out[2 * i]     = HEX_DIGITS[b >> 4];
        out[2 * i + 1] = HEX_DIGITS[b & 0x0F];
    }
    out[2 * len] = '\0';
}

#define MAX_BINDINGS    32

typedef struct {
    uint16_t short_addr;
    uint8_t  endpoint;
    uint16_t cluster_id;
} stub_binding_t;

static hal_zigbee_endpoint *endpoints             = NULL;
static uint8_t endpoints_count                    = 0;
static hal_zigbee_network_status_t network_status =
    HAL_ZIGBEE_NETWORK_NOT_JOINED;
static hal_zcl_activity_callback_t zcl_activity_callback = NULL;
static uint32_t poll_rate_ms = 0;
static uint32_t coordinator_send_failures;
static uint32_t announce_failures;
static hal_zigbee_diagnostics_t diagnostics;
static hal_zigbee_graph_usage_t graph_usage;

static stub_binding_t bindings[MAX_BINDINGS];
static int            binding_count = 0;

static hal_zigbee_cluster *find_unique_server_cluster(uint8_t endpoint,
                                                      uint16_t cluster_id) {
    hal_zigbee_cluster *result = NULL;

    for (uint8_t i = 0; i < endpoints_count; i++) {
        if (endpoints[i].endpoint != endpoint) continue;
        for (uint8_t j = 0; j < endpoints[i].cluster_count; j++) {
            hal_zigbee_cluster *candidate = &endpoints[i].clusters[j];

            if (candidate->cluster_id == cluster_id &&
                candidate->role == HAL_ZIGBEE_ROLE_SERVER) {
                if (result != NULL) return NULL;

                result = candidate;
            }
        }
    }
    return result;
}

bool hal_zigbee_init(hal_zigbee_endpoint *ep_list, uint8_t ep_count) {
    hal_zigbee_descriptor_error_t validation =
        hal_zigbee_validate_platform_graph(ep_list, ep_count);

    if (validation != HAL_ZIGBEE_DESCRIPTOR_OK) {
        diagnostics.descriptor_validation_failures++;
        diagnostics.last_descriptor_error = validation;
        io_log("ZIGBEE", "Error: descriptor graph exceeds platform limits (%d)",
               validation);
        io_evt("zcl_init_failed error=%d", validation);
        return false;
    }

    endpoints       = ep_list;
    endpoints_count = ep_count;
    graph_usage     = hal_zigbee_graph_usage(ep_list, ep_count);

    io_log("ZIGBEE", "Initialized Zigbee with %d endpoints", ep_count);

    for (int i = 0; i < ep_count; i++) {
        io_log("ZIGBEE", "Endpoint %d: profile=0x%04x, device=0x%04x, clusters=%d",
               ep_list[i].endpoint, ep_list[i].profile_id, ep_list[i].device_id,
               ep_list[i].cluster_count);
        for (int j = 0; j < ep_list[i].cluster_count; j++) {
            io_log("ZIGBEE", "  Cluster %d: id=0x%04x, attrs=%d, role=%d", j,
                   ep_list[i].clusters[j].cluster_id,
                   ep_list[i].clusters[j].attribute_count,
                   ep_list[i].clusters[j].role == HAL_ZIGBEE_ROLE_SERVER);
            for (int k = 0; k < ep_list[i].clusters[j].attribute_count; k++) {
                io_log("ZIGBEE",
                       "    Attr %d: id=0x%04x, type=0x%02x, size=%d, flags=%d", k,
                       ep_list[i].clusters[j].attributes[k].attribute_id,
                       ep_list[i].clusters[j].attributes[k].data_type_id,
                       ep_list[i].clusters[j].attributes[k].size,
                       ep_list[i].clusters[j].attributes[k].flag);
            }
        }
    }
    return true;
}

hal_zigbee_graph_usage_t stub_zigbee_get_graph_usage(void) {
    return graph_usage;
}

hal_zigbee_diagnostics_t hal_zigbee_get_diagnostics(void) {
    return diagnostics;
}

hal_zigbee_network_status_t hal_zigbee_get_network_status(void) {
    return network_status;
}

static hal_network_status_change_callback_t network_status_change_callback =
    NULL;

void hal_register_on_network_status_change_callback(
    hal_network_status_change_callback_t callback) {
    network_status_change_callback = callback;
}

void hal_zigbee_leave_network(void) {
    if (network_status == HAL_ZIGBEE_NETWORK_NOT_JOINED) {
        io_log("ZIGBEE", "Cannot leave network - not joined");
        return;
    }
    io_evt("zcl_leave_network");
    io_log("ZIGBEE", "Leaving network");
    network_status = HAL_ZIGBEE_NETWORK_NOT_JOINED;
    if (network_status_change_callback != NULL) {
        network_status_change_callback(hal_zigbee_get_network_status());
    }
}

void hal_zigbee_start_network_steering() {
    io_log("ZIGBEE", "Starting network steering (joining)");
    io_evt("zcl_start_network_steering");
    network_status = HAL_ZIGBEE_NETWORK_JOINING;
    if (network_status_change_callback != NULL) {
        network_status_change_callback(hal_zigbee_get_network_status());
    }
}

void hal_zigbee_register_on_zcl_activity_callback(
    hal_zcl_activity_callback_t callback) {
    zcl_activity_callback = callback;
    io_log("ZIGBEE", "Registered ZCL activity callback");
}

void hal_zigbee_set_poll_rate_ms(uint32_t new_poll_rate_ms) {
    poll_rate_ms = new_poll_rate_ms;
    io_log("ZIGBEE", "Set poll rate to %u ms", (unsigned)new_poll_rate_ms);
}

uint32_t hal_zigbee_get_poll_rate_ms(void) {
    return poll_rate_ms;
}

/* Telink and Silabs report only attributes flagged HAL_ATTR_REPORT, so the
 * zcl_attr_change event (a report the stack would send) follows that rule. */
void hal_zigbee_notify_attribute_changed(uint8_t endpoint, uint16_t cluster_id,
                                         uint16_t attribute_id) {
    hal_zigbee_attribute *attribute = hal_zigbee_find_attribute_in_cluster(
        find_unique_server_cluster(endpoint, cluster_id), attribute_id);

    if (attribute == NULL || (attribute->flag & HAL_ATTR_REPORT) == 0) {
        io_log("ZIGBEE", "Not reportable: ep=%d, cluster=0x%04x, attr=0x%04x",
               endpoint, cluster_id, attribute_id);
        return;
    }
    io_log("ZIGBEE", "Attribute changed: ep=%d, cluster=0x%04x, attr=0x%04x",
           endpoint, cluster_id, attribute_id);
    io_evt("zcl_attr_change ep=%u cluster=0x%04X attr=0x%04X", endpoint,
           cluster_id, attribute_id);
}

stub_zigbee_report_config_status_t
stub_zigbee_configure_reporting(uint8_t endpoint, uint16_t cluster_id,
                                uint16_t attribute_id) {
    hal_zigbee_attribute *attribute = hal_zigbee_find_attribute_in_cluster(
        find_unique_server_cluster(endpoint, cluster_id), attribute_id);

    if (attribute == NULL) return STUB_ZIGBEE_REPORT_CONFIG_UNSUPPORTED_ATTRIBUTE;

    if ((attribute->flag & HAL_ATTR_REPORT) == 0)
        return STUB_ZIGBEE_REPORT_CONFIG_UNREPORTABLE_ATTRIBUTE;

    return STUB_ZIGBEE_REPORT_CONFIG_SUCCESS;
}

hal_zigbee_status_t hal_zigbee_send_cmd_to_bindings(const hal_zigbee_cmd *cmd) {
    if (!cmd)
        return HAL_ZIGBEE_ERR_BAD_ARG;

    if (network_status != HAL_ZIGBEE_NETWORK_JOINED) {
        io_log("ZIGBEE", "Cannot send command - not joined to network");
        return HAL_ZIGBEE_ERR_NOT_JOINED;
    }

    io_log("ZIGBEE", "Sending command: ep=%d, cluster=0x%04x, cmd=0x%02x, len=%d",
           cmd->endpoint, cmd->cluster_id, cmd->command_id, cmd->payload_len);

    char buffer[cmd->payload_len * 2 + 1];
    bytes_to_hexstr(cmd->payload, cmd->payload_len, buffer);

    io_evt("zcl_cmd_send ep=%u cluster=0x%04X cmd=0x%02X len=%u data_hex=%s",
           cmd->endpoint, cmd->cluster_id, cmd->command_id, cmd->payload_len,
           buffer);

    // Simulate sending to all relevant bindings
    int sent_count = 0;
    for (int i = 0; i < binding_count; i++) {
        if (bindings[i].endpoint == cmd->endpoint &&
            bindings[i].cluster_id == cmd->cluster_id) {
            io_log("ZIGBEE", "Sent to binding %d (addr=0x%04x)", i,
                   bindings[i].short_addr);
            sent_count++;
        }
    }

    if (sent_count == 0) {
        io_log("ZIGBEE", "No matching bindings found");
    }

    return HAL_ZIGBEE_OK;
}

hal_zigbee_status_t
hal_zigbee_send_cmd_to_coordinator(const hal_zigbee_cmd *cmd) {
    char buffer[HAL_ZIGBEE_CMD_MAX_PAYLOAD * 2 + 1];

    if (cmd == NULL || cmd->payload_len > HAL_ZIGBEE_CMD_MAX_PAYLOAD ||
        (cmd->payload_len != 0 && cmd->payload == NULL)) {
        return HAL_ZIGBEE_ERR_BAD_ARG;
    }
    if (network_status != HAL_ZIGBEE_NETWORK_JOINED) {
        return HAL_ZIGBEE_ERR_NOT_JOINED;
    }
    if (coordinator_send_failures != 0) {
        coordinator_send_failures--;
        return HAL_ZIGBEE_ERR_SEND_FAILED;
    }
    bytes_to_hexstr(cmd->payload, cmd->payload_len, buffer);
    io_evt("zcl_cmd_send dst=coordinator ep=%u cluster=0x%04X cmd=0x%02X "
           "len=%u data_hex=%s",
           cmd->endpoint, cmd->cluster_id, cmd->command_id, cmd->payload_len,
           buffer);
    return HAL_ZIGBEE_OK;
}

hal_zigbee_status_t
hal_zigbee_send_report_attr(uint8_t endpoint, uint16_t cluster_id,
                            uint16_t attr_id, uint8_t zcl_type_id,
                            const void *value, uint8_t value_len) {
    hal_zigbee_cluster *  cluster   = find_unique_server_cluster(endpoint, cluster_id);
    hal_zigbee_attribute *attribute = hal_zigbee_find_attribute_in_cluster(
        cluster, attr_id);

    if (!value || attribute == NULL || (attribute->flag & HAL_ATTR_REPORT) == 0 ||
        attribute->data_type_id != zcl_type_id || attribute->size != value_len)
        return HAL_ZIGBEE_ERR_BAD_ARG;

    if (network_status != HAL_ZIGBEE_NETWORK_JOINED) {
        io_log("ZIGBEE", "Cannot send report - not joined to network");
        return HAL_ZIGBEE_ERR_NOT_JOINED;
    }

    io_log("ZIGBEE",
           "Sending attribute report: ep=%d, cluster=0x%04x, attr=0x%04x, "
           "type=0x%02x, len=%d",
           endpoint, cluster_id, attr_id, zcl_type_id, value_len);

    {
        char buffer[value_len * 2 + 1];

        bytes_to_hexstr((const uint8_t *)value, value_len, buffer);
        io_evt("zcl_attr_report ep=%u cluster=0x%04X attr=0x%04X type=0x%02X "
               "len=%u data_hex=%s",
               endpoint, cluster_id, attr_id, zcl_type_id, value_len, buffer);
    }

    return HAL_ZIGBEE_OK;
}

hal_zigbee_status_t hal_zigbee_send_announce(void) {
    if (announce_failures != 0) {
        announce_failures--;
        io_log("ZIGBEE", "Rejecting Zigbee announce");
        return HAL_ZIGBEE_ERR_SEND_FAILED;
    }
    io_log("ZIGBEE", "Sending Zigbee announce");
    io_evt("zdo_announce");
    return HAL_ZIGBEE_OK;
}

void stub_zigbee_set_network_status(hal_zigbee_network_status_t status) {
    network_status = status;
    io_log("ZIGBEE", "Network status set to %d", network_status);
    if (network_status_change_callback != NULL) {
        network_status_change_callback(hal_zigbee_get_network_status());
    }
}

void stub_zigbee_reject_next_coordinator_sends(uint32_t count) {
    coordinator_send_failures = count;
}

void stub_zigbee_reject_next_announces(uint32_t count) {
    announce_failures = count;
}

void stub_zigbee_add_binding(uint16_t short_addr, uint8_t endpoint,
                             uint16_t cluster_id) {
    if (binding_count >= MAX_BINDINGS) {
        io_log("ZIGBEE", "Cannot add binding - table full");
        return;
    }

    bindings[binding_count].short_addr = short_addr;
    bindings[binding_count].endpoint   = endpoint;
    bindings[binding_count].cluster_id = cluster_id;
    binding_count++;

    io_log("ZIGBEE", "Added binding: addr=0x%04x, ep=%d, cluster=0x%04x",
           short_addr, endpoint, cluster_id);
}

void stub_zigbee_clear_bindings(void) {
    binding_count = 0;
    io_log("ZIGBEE", "Cleared all bindings");
}

hal_zigbee_endpoint *stub_zigbee_get_endpoints(uint8_t *count) {
    if (count)
        *count = endpoints_count;
    return endpoints;
}

static hal_zigbee_cmd_result_t
stub_zigbee_simulate_command_internal(uint8_t endpoint, uint16_t cluster_id,
                                      uint8_t command_id, void *payload,
                                      uint16_t payload_len,
                                      bool trigger_activity) {
    io_log("ZIGBEE", "Simulating command: ep=%d, cluster=0x%04x, cmd=0x%02x",
           endpoint, cluster_id, command_id);

    if (trigger_activity && zcl_activity_callback != NULL) {
        zcl_activity_callback();
    }

    hal_zigbee_cluster *cluster = hal_zigbee_find_cluster(
        endpoints, endpoints_count, endpoint, cluster_id);
    if (cluster != NULL && cluster->role == HAL_ZIGBEE_ROLE_SERVER &&
        cluster->cmd_callback != NULL) {
        const hal_zigbee_incoming_command_t command = {
            .source_endpoint       =                               1,
            .destination_endpoint  = endpoint,
            .source_address        =                               0,
            .cluster_id            = cluster_id,
            .command_id            = command_id,
            .cluster_specific      =                               1,
            .manufacturer_specific = cluster->manufacturer_code != 0,
            .manufacturer_code     = cluster->manufacturer_code,
            .direction             = HAL_ZIGBEE_DIR_CLIENT_TO_SERVER,
            .payload     = payload,
            .payload_len = payload_len,
        };

        return cluster->cmd_callback(&command);
    }

    return HAL_ZIGBEE_CMD_SKIPPED;
}

// Simulate receiving a ZCL command
hal_zigbee_cmd_result_t stub_zigbee_simulate_command(uint8_t endpoint,
                                                     uint16_t cluster_id,
                                                     uint8_t command_id,
                                                     void *payload,
                                                     uint16_t payload_len) {
    return stub_zigbee_simulate_command_internal(endpoint, cluster_id,
                                                 command_id, payload,
                                                 payload_len, true);
}

hal_zigbee_cmd_result_t
stub_zigbee_simulate_command_without_activity(uint8_t endpoint,
                                              uint16_t cluster_id,
                                              uint8_t command_id,
                                              void *payload,
                                              uint16_t payload_len) {
    return stub_zigbee_simulate_command_internal(endpoint, cluster_id,
                                                 command_id, payload,
                                                 payload_len, false);
}

/* One Write Attributes record, checked the way the Telink pre-write hook
 * checks it: writable server attribute, encoded size within `available`
 * bytes and within the attribute storage. */
typedef struct {
    hal_zigbee_cluster *  cluster;
    hal_zigbee_attribute *attribute;
    const uint8_t *       candidate;
    uint16_t              candidate_len;
} stub_write_record_t;

/* The record as it would arrive in a Write Attributes frame, checked the way
 * the patched Telink foundation dispatch checks it before the SDK parses it. */
static bool stub_write_record_is_well_formed(const hal_zigbee_attribute *attribute,
                                             const uint8_t *value, uint16_t len) {
    uint8_t frame[3 + 512];

    if (len > sizeof(frame) - 3) return false;

    frame[0] = (uint8_t)attribute->attribute_id;
    frame[1] = (uint8_t)(attribute->attribute_id >> 8);
    frame[2] = attribute->data_type_id;
    memcpy(&frame[3], value, len);
    return zcl_frame_is_well_formed(ZCL_FRAME_CMD_WRITE, frame, (uint16_t)(3 + len),
                                    ZCL_FRAME_MAX_RECORDS);
}

static hal_zigbee_cmd_result_t stub_write_prepare(
    uint8_t endpoint, uint16_t cluster_id, uint16_t attribute_id,
    const uint8_t *candidate, uint16_t available, stub_write_record_t *record) {
    record->cluster   = find_unique_server_cluster(endpoint, cluster_id);
    record->attribute = hal_zigbee_find_attribute_in_cluster(record->cluster,
                                                             attribute_id);
    if (record->attribute == NULL || candidate == NULL ||
        (record->attribute->flag & HAL_ATTR_WRITE) == 0) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    record->candidate = candidate;
    if (!stub_write_record_is_well_formed(record->attribute, candidate, available)) {
        return HAL_ZIGBEE_MALFORMED_COMMAND;
    }
    if (!hal_zigbee_type_is_string(record->attribute->data_type_id)) {
        record->candidate_len = available;
        return available == record->attribute->size ?
               HAL_ZIGBEE_CMD_PROCESSED : HAL_ZIGBEE_INVALID_VALUE;
    }
    record->candidate_len = hal_zigbee_attr_encoded_size(
        record->attribute->data_type_id, candidate, available);
    if (record->candidate_len == 0) return HAL_ZIGBEE_MALFORMED_COMMAND;

    return record->candidate_len <= record->attribute->size ?
           HAL_ZIGBEE_CMD_PROCESSED : HAL_ZIGBEE_INVALID_VALUE;
}

static hal_zigbee_cmd_result_t stub_write_phase(
    uint8_t endpoint, const stub_write_record_t *record,
    hal_zigbee_attr_write_phase_t phase) {
    if (record->cluster->attr_write_callback == NULL) return HAL_ZIGBEE_CMD_PROCESSED;

    return record->cluster->attr_write_callback(
        endpoint, record->attribute->attribute_id, record->candidate,
        record->candidate_len, phase);
}

/* VALIDATE, COMMIT, then copy into storage: the order of the patched Telink
 * SDK, where zcl_setAttrVal runs after the pre-write hook returns. */
static hal_zigbee_cmd_result_t stub_write_apply(uint8_t endpoint,
                                                const stub_write_record_t *record) {
    hal_zigbee_cmd_result_t result =
        stub_write_phase(endpoint, record, HAL_ZIGBEE_ATTR_VALIDATE);

    if (result != HAL_ZIGBEE_CMD_PROCESSED) return result;

    result = stub_write_phase(endpoint, record, HAL_ZIGBEE_ATTR_COMMIT);
    if (result != HAL_ZIGBEE_CMD_PROCESSED) return result;

    memcpy(record->attribute->value, record->candidate, record->candidate_len);
    return HAL_ZIGBEE_CMD_PROCESSED;
}

hal_zigbee_cmd_result_t stub_simulate_zigbee_attribute_write(
    uint8_t endpoint, uint16_t cluster_id, uint16_t attribute_id,
    const uint8_t *candidate, uint16_t candidate_len) {
    stub_write_record_t     record;
    hal_zigbee_cmd_result_t result;

    if (zcl_activity_callback != NULL) {
        zcl_activity_callback();
    }
    result = stub_write_prepare(endpoint, cluster_id, attribute_id, candidate,
                                candidate_len, &record);
    if (result != HAL_ZIGBEE_CMD_PROCESSED) return result;

    return stub_write_apply(endpoint, &record);
}

hal_zigbee_cmd_result_t stub_simulate_zigbee_attribute_write_undivided(
    uint8_t endpoint, uint16_t cluster_id, uint8_t count,
    const uint16_t *attribute_ids, const uint8_t *const *candidates,
    const uint16_t *candidate_lens) {
    stub_write_record_t     records[STUB_ZIGBEE_MAX_WRITE_RECORDS];
    hal_zigbee_cmd_result_t result;

    if (count == 0 || count > STUB_ZIGBEE_MAX_WRITE_RECORDS) {
        return HAL_ZIGBEE_MALFORMED_COMMAND;
    }
    if (zcl_activity_callback != NULL) {
        zcl_activity_callback();
    }
    /* First pass: every record is checked and validated, nothing changes. */
    for (uint8_t i = 0; i < count; i++) {
        result = stub_write_prepare(endpoint, cluster_id, attribute_ids[i],
                                    candidates[i], candidate_lens[i], &records[i]);
        if (result == HAL_ZIGBEE_CMD_PROCESSED) {
            result = stub_write_phase(endpoint, &records[i], HAL_ZIGBEE_ATTR_VALIDATE);
        }
        if (result != HAL_ZIGBEE_CMD_PROCESSED) return result;
    }
    /* Second pass: the SDK writes every record with its normal write path. */
    for (uint8_t i = 0; i < count; i++) {
        result = stub_write_apply(endpoint, &records[i]);
        if (result != HAL_ZIGBEE_CMD_PROCESSED) return result;
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}
