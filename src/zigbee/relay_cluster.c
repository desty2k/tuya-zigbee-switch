#include "relay_cluster.h"
#include "cluster_common.h"
#include "zcl_cluster_defs.h"
#include "zcl_ids.h"
#include "zcl_types.h"
#include "device_config/capability_state.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"

hal_zigbee_cmd_result_t relay_cluster_callback(zigbee_relay_cluster *cluster,
                                               uint8_t command_id,
                                               void *cmd_payload,
                                               uint16_t cmd_payload_len);
hal_zigbee_cmd_result_t relay_cluster_callback_trampoline(
    const hal_zigbee_incoming_command_t *command);

hal_zigbee_cmd_result_t relay_cluster_level_callback(zigbee_relay_cluster *cluster,
                                                     uint8_t command_id,
                                                     void *cmd_payload,
                                                     uint16_t cmd_payload_len);
hal_zigbee_cmd_result_t relay_cluster_level_callback_trampoline(
    const hal_zigbee_incoming_command_t *command);

static void relay_cluster_on_relay_change(void *param, uint8_t relay_id,
                                          bool is_on,
                                          relay_request_source_t source);
static hal_zigbee_cmd_result_t relay_cluster_result_to_zigbee(
    relay_result_t result);
static void relay_cluster_on_write_attr(zigbee_relay_cluster *cluster,
                                        uint16_t attribute_id);

void relay_cluster_store_attrs_to_nv(zigbee_relay_cluster *cluster);
void relay_cluster_load_attrs_from_nv(zigbee_relay_cluster *cluster);
void relay_cluster_handle_startup_mode(zigbee_relay_cluster *cluster);

void sync_indicator_led(zigbee_relay_cluster *cluster);

zigbee_relay_cluster *relay_cluster_by_endpoint[HAL_ZIGBEE_ENDPOINT_ID_MAX + 1];

static zigbee_relay_cluster *relay_cluster_find(uint8_t endpoint) {
    if (endpoint > HAL_ZIGBEE_ENDPOINT_ID_MAX) {
        return NULL;
    }
    return relay_cluster_by_endpoint[endpoint];
}

static hal_zigbee_cmd_result_t relay_cluster_attr_write(
    uint8_t endpoint, uint16_t attribute_id, const uint8_t *candidate,
    uint16_t candidate_len, hal_zigbee_attr_write_phase_t phase) {
    zigbee_relay_cluster *cluster = relay_cluster_find(endpoint);

    if (cluster == NULL || candidate == NULL) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    for (uint8_t i = 0; i < sizeof(cluster->attr_infos) / sizeof(cluster->attr_infos[0]); i++) {
        if (cluster->attr_infos[i].attribute_id != attribute_id) continue;
        if (candidate_len != cluster->attr_infos[i].size) return HAL_ZIGBEE_INVALID_VALUE;

        if (phase == HAL_ZIGBEE_ATTR_COMMIT) {
            relay_cluster_on_write_attr(cluster, attribute_id);
        }
        return HAL_ZIGBEE_CMD_PROCESSED;
    }
    return HAL_ZIGBEE_CMD_SKIPPED;
}

void update_relay_clusters() {
    for (int i = 0; i <= HAL_ZIGBEE_ENDPOINT_ID_MAX; i++) {
        if (relay_cluster_by_endpoint[i] != NULL) {
            sync_indicator_led(relay_cluster_by_endpoint[i]);
        }
    }
}

void relay_cluster_add_to_endpoint(zigbee_relay_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    if (!hal_zigbee_endpoint_reserve_clusters(endpoint,
                                              endpoint->cluster_capacity, 2)) {
        return;
    }

    relay_cluster_by_endpoint[endpoint->endpoint] = cluster;
    cluster->endpoint        = endpoint->endpoint;
    cluster->interlock_group =
        relay_ctrl_get_interlock_group(cluster->relay_id);
    relay_cluster_load_attrs_from_nv(cluster);
    relay_ctrl_set_inching_ms(cluster->relay_id, cluster->inching_ms);
    relay_ctrl_set_interlock_group(cluster->relay_id,
                                   cluster->interlock_group);

    cluster->on_off = relay_ctrl_is_on(cluster->relay_id);
    if (!relay_ctrl_subscribe(cluster->relay_id, relay_cluster_on_relay_change,
                              cluster)) {
        return;
    }

    relay_cluster_handle_startup_mode(cluster);
    sync_indicator_led(cluster);

    SETUP_ATTR(0, ZCL_ATTR_ONOFF, ZCL_DATA_TYPE_BOOLEAN,
               (HAL_ATTR_READ | HAL_ATTR_REPORT), cluster->on_off);
    SETUP_ATTR(1, ZCL_ATTR_START_UP_ONOFF, ZCL_DATA_TYPE_ENUM8, (HAL_ATTR_READ | HAL_ATTR_WRITE),
               cluster->startup_mode);
    SETUP_ATTR(2, ZCL_ATTR_ONOFF_INCHING_DURATION, ZCL_DATA_TYPE_UINT16,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->inching_ms);
    SETUP_ATTR(3, ZCL_ATTR_ONOFF_INTERLOCK_GROUP, ZCL_DATA_TYPE_UINT8,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->interlock_group);
    if (cluster->indicator_led != NULL) {
        SETUP_ATTR(4, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_DATA_TYPE_ENUM8,
                   (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->indicator_led_mode);
        SETUP_ATTR(5, ZCL_ATTR_ONOFF_INDICATOR_STATE, ZCL_DATA_TYPE_BOOLEAN,
                   (HAL_ATTR_READ | HAL_ATTR_WRITE | HAL_ATTR_REPORT),
                   cluster->indicator_state);
    }

    hal_zigbee_cluster on_off_cluster = {
        .cluster_id          = ZCL_CLUSTER_ON_OFF,
        .role                = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count     = cluster->indicator_led != NULL ? 6 : 4,
        .attributes          = cluster->attr_infos,
        .cmd_callback        = relay_cluster_callback_trampoline,
        .attr_write_callback = relay_cluster_attr_write,
    };
    hal_zigbee_cluster level_cluster = {
        .cluster_id   = ZCL_CLUSTER_LEVEL_CONTROL,
        .role         = HAL_ZIGBEE_ROLE_SERVER,
        .cmd_callback = relay_cluster_level_callback_trampoline,
    };

    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity,
                                    &on_off_cluster);
    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity,
                                    &level_cluster);
}

hal_zigbee_cmd_result_t relay_cluster_callback_trampoline(
    const hal_zigbee_incoming_command_t *command) {
    if (command == NULL) return HAL_ZIGBEE_CMD_SKIPPED;

    zigbee_relay_cluster *cluster = relay_cluster_find(command->destination_endpoint);

    return cluster == NULL ? HAL_ZIGBEE_CMD_SKIPPED :
           relay_cluster_callback(cluster, command->command_id,
                                  (void *)command->payload, command->payload_len);
}

static hal_zigbee_cmd_result_t relay_cluster_result_to_zigbee(
    relay_result_t result) {
    switch (result) {
    case RELAY_RESULT_APPLIED:
    case RELAY_RESULT_UNCHANGED:
    case RELAY_RESULT_DEFERRED:
        return HAL_ZIGBEE_CMD_PROCESSED;

    case RELAY_RESULT_BLOCKED:
        return HAL_ZIGBEE_CMD_SKIPPED;

    case RELAY_RESULT_INVALID:
    default:
        return HAL_ZIGBEE_INVALID_VALUE;
    }
}

hal_zigbee_cmd_result_t relay_cluster_callback(zigbee_relay_cluster *cluster,
                                               uint8_t command_id,
                                               void *cmd_payload,
                                               uint16_t cmd_payload_len) {
    switch (command_id) {
    case ZCL_CMD_ONOFF_ON:
    case ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE:
        return relay_cluster_result_to_zigbee(relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = cluster->inching_ms == 0 ? RELAY_REQUEST_ON
                                                 : RELAY_REQUEST_PULSE,
            .source = RELAY_SOURCE_ZIGBEE,
        }));

    case ZCL_CMD_ONOFF_OFF:
    case ZCL_CMD_OFF_WITH_EFFECT:
        return relay_cluster_result_to_zigbee(relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = RELAY_REQUEST_OFF,
            .source   = RELAY_SOURCE_ZIGBEE,
        }));

    case ZCL_CMD_ONOFF_TOGGLE:
        return relay_cluster_result_to_zigbee(relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = RELAY_REQUEST_TOGGLE,
            .source   = RELAY_SOURCE_ZIGBEE,
        }));

    case ZCL_CMD_ON_WITH_TIMED_OFF: {
        uint8_t *payload = (uint8_t *)cmd_payload;

        if (payload == NULL || cmd_payload_len < 5) {
            return HAL_ZIGBEE_MALFORMED_COMMAND;
        }
        uint16_t on_time = (uint16_t)payload[1] |
                           ((uint16_t)payload[2] << 8);
        return relay_cluster_result_to_zigbee(relay_ctrl_submit(&(relay_request_t){
                .relay_id    = cluster->relay_id,
                .type        = RELAY_REQUEST_ON_TIMED,
                .duration_ms = (uint32_t)on_time * 100,
                .source      = RELAY_SOURCE_ZIGBEE,
            }));
    }

    default:
        printf("Unknown OnOff command: %d\r\n", command_id);
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
}

hal_zigbee_cmd_result_t relay_cluster_level_callback_trampoline(
    const hal_zigbee_incoming_command_t *command) {
    if (command == NULL) return HAL_ZIGBEE_CMD_SKIPPED;

    zigbee_relay_cluster *cluster = relay_cluster_find(command->destination_endpoint);

    return cluster == NULL ? HAL_ZIGBEE_CMD_SKIPPED :
           relay_cluster_level_callback(cluster, command->command_id,
                                        (void *)command->payload,
                                        command->payload_len);
}

hal_zigbee_cmd_result_t relay_cluster_level_callback(zigbee_relay_cluster *cluster,
                                                     uint8_t command_id,
                                                     void *cmd_payload,
                                                     uint16_t cmd_payload_len) {
    switch (command_id) {
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:
        if (cmd_payload == NULL || cmd_payload_len < 1) {
            return HAL_ZIGBEE_MALFORMED_COMMAND;
        }
        uint8_t level = *(uint8_t *)cmd_payload;
        return relay_cluster_result_to_zigbee(relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = level == 0 ? RELAY_REQUEST_OFF : RELAY_REQUEST_ON,
            .source   = RELAY_SOURCE_ZIGBEE,
        }));

    default:
        printf("Unknown LevelCtrl command: %d\r\n", command_id);
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
}

void sync_indicator_led(zigbee_relay_cluster *cluster) {
    if (cluster->indicator_led == NULL) {
        return;
    }

    if (cluster->indicator_led_mode != ZCL_ONOFF_INDICATOR_MODE_MANUAL) {
        if (cluster->indicator_led_mode == ZCL_ONOFF_INDICATOR_MODE_SAME) {
            cluster->indicator_state = cluster->on_off;
        } else {
            cluster->indicator_state = !cluster->on_off;
        }
    }

    indicator_feedback_set_base_state(cluster->indicator_led,
                                      cluster->indicator_state);

    hal_zigbee_notify_attribute_changed(cluster->endpoint, ZCL_CLUSTER_ON_OFF,
                                        ZCL_ATTR_ONOFF_INDICATOR_STATE);
}

static void relay_cluster_on_relay_change(void *param, uint8_t relay_id,
                                          bool is_on,
                                          relay_request_source_t source) {
    zigbee_relay_cluster *cluster = (zigbee_relay_cluster *)param;

    (void)relay_id;
    (void)source;
    cluster->on_off = is_on;
    hal_zigbee_notify_attribute_changed(cluster->endpoint, ZCL_CLUSTER_ON_OFF,
                                        ZCL_ATTR_ONOFF);
    sync_indicator_led(cluster);
    if (cluster->startup_mode == ZCL_START_UP_ONOFF_SET_ONOFF_TOGGLE ||
        cluster->startup_mode == ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS) {
        relay_cluster_store_attrs_to_nv(cluster);
    }
}

static void relay_cluster_on_write_attr(zigbee_relay_cluster *cluster,
                                        uint16_t attribute_id) {
    if (attribute_id == ZCL_ATTR_ONOFF_INCHING_DURATION) {
        relay_ctrl_set_inching_ms(cluster->relay_id, cluster->inching_ms);
    }
    if (attribute_id == ZCL_ATTR_ONOFF_INTERLOCK_GROUP) {
        relay_ctrl_set_interlock_group(cluster->relay_id,
                                       cluster->interlock_group);
    }
    if (attribute_id == ZCL_ATTR_ONOFF_INDICATOR_STATE) {
        sync_indicator_led(cluster);
    }
    if (cluster->indicator_led_mode != ZCL_ONOFF_INDICATOR_MODE_MANUAL) {
        sync_indicator_led(cluster);
    }

    relay_cluster_store_attrs_to_nv(cluster);
}

static relay_state_record_t nv_config_buffer;

void relay_cluster_store_attrs_to_nv(zigbee_relay_cluster *cluster) {
    nv_config_buffer = (relay_state_record_t){
        .on_off         = cluster->on_off, .startup_mode = cluster->startup_mode,
        .indicator_mode = cluster->indicator_led_mode, .indicator_state = cluster->indicator_state,
        .inching_ms     = cluster->inching_ms, .interlock_group = cluster->interlock_group,
    };
    (void)capability_state_store_relay(cluster->relay_idx, &nv_config_buffer);
}

void relay_cluster_load_attrs_from_nv(zigbee_relay_cluster *cluster) {
    if (!capability_state_load_relay(cluster->relay_idx, &nv_config_buffer)) return;

    cluster->startup_mode       = nv_config_buffer.startup_mode;
    cluster->indicator_led_mode = nv_config_buffer.indicator_mode;
    cluster->indicator_state    = nv_config_buffer.indicator_state;
    cluster->inching_ms         = nv_config_buffer.inching_ms;
    cluster->interlock_group    = nv_config_buffer.interlock_group;
}

void relay_cluster_handle_startup_mode(zigbee_relay_cluster *cluster) {
    if (!capability_state_load_relay(cluster->relay_idx, &nv_config_buffer)) return;

    uint8_t prev_on = nv_config_buffer.on_off;

    relay_request_t request = {
        .relay_id = cluster->relay_id,
        .source   = RELAY_SOURCE_STARTUP,
    };

    switch (cluster->startup_mode) {
    case ZCL_START_UP_ONOFF_SET_ONOFF_TO_OFF:
        request.type = RELAY_REQUEST_OFF;
        break;

    case ZCL_START_UP_ONOFF_SET_ONOFF_TO_ON:
        request.type = RELAY_REQUEST_ON;
        break;

    case ZCL_START_UP_ONOFF_SET_ONOFF_TOGGLE:
        request.type = prev_on ? RELAY_REQUEST_OFF : RELAY_REQUEST_ON;
        break;

    case ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS:
        request.type = prev_on ? RELAY_REQUEST_ON : RELAY_REQUEST_OFF;
        break;

    default:
        return;
    }

    relay_ctrl_submit(&request);
}
