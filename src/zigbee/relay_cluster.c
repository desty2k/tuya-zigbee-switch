#include "relay_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "device_config/nvm_items.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"

hal_zigbee_cmd_result_t relay_cluster_callback(zigbee_relay_cluster *cluster,
                                               uint8_t command_id,
                                               void *cmd_payload,
                                               uint16_t cmd_payload_len);
hal_zigbee_cmd_result_t relay_cluster_callback_trampoline(uint8_t endpoint,
                                                          uint16_t cluster_id,
                                                          uint8_t command_id,
                                                          void *cmd_payload,
                                                          uint16_t cmd_payload_len);

hal_zigbee_cmd_result_t relay_cluster_level_callback(zigbee_relay_cluster *cluster,
                                                     uint8_t command_id,
                                                     void *cmd_payload,
                                                     uint16_t cmd_payload_len);
hal_zigbee_cmd_result_t relay_cluster_level_callback_trampoline(uint8_t endpoint,
                                                                uint16_t cluster_id,
                                                                uint8_t command_id,
                                                                void *cmd_payload,
                                                                uint16_t cmd_payload_len);

static void relay_cluster_on_relay_change(void *param, uint8_t relay_id,
                                          bool is_on);
void relay_cluster_on_write_attr(zigbee_relay_cluster *cluster,
                                 uint16_t attribute_id);

void relay_cluster_store_attrs_to_nv(zigbee_relay_cluster *cluster);
void relay_cluster_load_attrs_from_nv(zigbee_relay_cluster *cluster);
void relay_cluster_handle_startup_mode(zigbee_relay_cluster *cluster);

void sync_indicator_led(zigbee_relay_cluster *cluster);

zigbee_relay_cluster *relay_cluster_by_endpoint[10];

void relay_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                  uint16_t attribute_id) {
    relay_cluster_on_write_attr(relay_cluster_by_endpoint[endpoint],
                                attribute_id);
}

void update_relay_clusters() {
    for (int i = 0; i < 10; i++) {
        if (relay_cluster_by_endpoint[i] != NULL) {
            sync_indicator_led(relay_cluster_by_endpoint[i]);
        }
    }
}

void relay_cluster_add_to_endpoint(zigbee_relay_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    relay_cluster_by_endpoint[endpoint->endpoint] = cluster;
    cluster->endpoint        = endpoint->endpoint;
    cluster->interlock_group =
        relay_ctrl_get_interlock_group(cluster->relay_id);
    relay_cluster_load_attrs_from_nv(cluster);
    relay_ctrl_set_inching_ms(cluster->relay_id, cluster->inching_ms);
    relay_ctrl_set_interlock_group(cluster->relay_id,
                                   cluster->interlock_group);

    cluster->on_off = relay_ctrl_is_on(cluster->relay_id);
    relay_ctrl_set_state_callback(cluster->relay_id,
                                  relay_cluster_on_relay_change, cluster);

    relay_cluster_handle_startup_mode(cluster);
    sync_indicator_led(cluster);

    SETUP_ATTR(0, ZCL_ATTR_ONOFF, ZCL_DATA_TYPE_BOOLEAN, ATTR_READONLY,
               cluster->on_off);
    SETUP_ATTR(1, ZCL_ATTR_START_UP_ONOFF, ZCL_DATA_TYPE_ENUM8, ATTR_WRITABLE,
               cluster->startup_mode);
    SETUP_ATTR(2, ZCL_ATTR_ONOFF_INCHING_DURATION, ZCL_DATA_TYPE_UINT16,
               ATTR_WRITABLE, cluster->inching_ms);
    SETUP_ATTR(3, ZCL_ATTR_ONOFF_INTERLOCK_GROUP, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, cluster->interlock_group);
    if (cluster->indicator_led != NULL) {
        SETUP_ATTR(4, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_DATA_TYPE_ENUM8,
                   ATTR_WRITABLE, cluster->indicator_led_mode);
        SETUP_ATTR(5, ZCL_ATTR_ONOFF_INDICATOR_STATE, ZCL_DATA_TYPE_BOOLEAN,
                   ATTR_WRITABLE, cluster->indicator_state);
    }

    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_ON_OFF;
    endpoint->clusters[endpoint->cluster_count].attribute_count =
        cluster->indicator_led != NULL ? 6 : 4;
    endpoint->clusters[endpoint->cluster_count].attributes   = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server    = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback =
        relay_cluster_callback_trampoline;
    endpoint->cluster_count++;

    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_LEVEL_CONTROL;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 0;
    endpoint->clusters[endpoint->cluster_count].attributes      = NULL;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    =
        relay_cluster_level_callback_trampoline;
    endpoint->cluster_count++;
}

hal_zigbee_cmd_result_t relay_cluster_callback_trampoline(uint8_t endpoint,
                                                          uint16_t cluster_id,
                                                          uint8_t command_id,
                                                          void *cmd_payload,
                                                          uint16_t cmd_payload_len) {
    return relay_cluster_callback(relay_cluster_by_endpoint[endpoint], command_id,
                                  cmd_payload, cmd_payload_len);
}

hal_zigbee_cmd_result_t relay_cluster_callback(zigbee_relay_cluster *cluster,
                                               uint8_t command_id,
                                               void *cmd_payload,
                                               uint16_t cmd_payload_len) {
    switch (command_id) {
    case ZCL_CMD_ONOFF_ON:
    case ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE:
        return relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = cluster->inching_ms == 0 ? RELAY_REQUEST_ON
                                                 : RELAY_REQUEST_PULSE,
            .source = RELAY_SOURCE_ZIGBEE,
        });

    case ZCL_CMD_ONOFF_OFF:
    case ZCL_CMD_OFF_WITH_EFFECT:
        return relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = RELAY_REQUEST_OFF,
            .source   = RELAY_SOURCE_ZIGBEE,
        });

    case ZCL_CMD_ONOFF_TOGGLE:
        return relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = RELAY_REQUEST_TOGGLE,
            .source   = RELAY_SOURCE_ZIGBEE,
        });

    case ZCL_CMD_ON_WITH_TIMED_OFF: {
        uint8_t *payload = (uint8_t *)cmd_payload;

        if (payload == NULL || cmd_payload_len < 5) {
            return HAL_ZIGBEE_MALFORMED_COMMAND;
        }
        uint16_t on_time = (uint16_t)payload[1] |
                           ((uint16_t)payload[2] << 8);
        return relay_ctrl_submit(&(relay_request_t){
                .relay_id    = cluster->relay_id,
                .type        = RELAY_REQUEST_ON_TIMED,
                .duration_ms = (uint32_t)on_time * 100,
                .source      = RELAY_SOURCE_ZIGBEE,
            });
    }

    default:
        printf("Unknown OnOff command: %d\r\n", command_id);
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
}

hal_zigbee_cmd_result_t relay_cluster_level_callback_trampoline(uint8_t endpoint,
                                                                uint16_t cluster_id,
                                                                uint8_t command_id,
                                                                void *cmd_payload,
                                                                uint16_t cmd_payload_len) {
    return relay_cluster_level_callback(relay_cluster_by_endpoint[endpoint], command_id,
                                        cmd_payload, cmd_payload_len);
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
        return relay_ctrl_submit(&(relay_request_t){
            .relay_id = cluster->relay_id,
            .type     = level == 0 ? RELAY_REQUEST_OFF : RELAY_REQUEST_ON,
            .source   = RELAY_SOURCE_ZIGBEE,
        });

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
                                          bool is_on) {
    zigbee_relay_cluster *cluster = (zigbee_relay_cluster *)param;

    (void)relay_id;
    cluster->on_off = is_on;
    hal_zigbee_notify_attribute_changed(cluster->endpoint, ZCL_CLUSTER_ON_OFF,
                                        ZCL_ATTR_ONOFF);
    sync_indicator_led(cluster);
    if (cluster->startup_mode == ZCL_START_UP_ONOFF_SET_ONOFF_TOGGLE ||
        cluster->startup_mode == ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS) {
        relay_cluster_store_attrs_to_nv(cluster);
    }
}

void relay_cluster_on_write_attr(zigbee_relay_cluster *cluster,
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

typedef struct {
    uint8_t  on_off;
    uint8_t  startup_mode;
    uint8_t  indicator_led_mode;
    uint8_t  indicator_led_on;
    uint16_t inching_ms;
    uint8_t  interlock_group;
} zigbee_relay_cluster_config;

static zigbee_relay_cluster_config nv_config_buffer;

void relay_cluster_store_attrs_to_nv(zigbee_relay_cluster *cluster) {
    nv_config_buffer.on_off             = cluster->on_off;
    nv_config_buffer.startup_mode       = cluster->startup_mode;
    nv_config_buffer.indicator_led_mode = cluster->indicator_led_mode;
    if (cluster->indicator_led != NULL) {
        nv_config_buffer.indicator_led_on = cluster->indicator_state;
    }
    nv_config_buffer.inching_ms      = cluster->inching_ms;
    nv_config_buffer.interlock_group = cluster->interlock_group;

    hal_nvm_write(NV_ITEM_RELAY_CLUSTER_DATA(cluster->relay_idx),
                  sizeof(zigbee_relay_cluster_config),
                  (uint8_t *)&nv_config_buffer);
}

void relay_cluster_load_attrs_from_nv(zigbee_relay_cluster *cluster) {
    hal_nvm_status_t st = hal_nvm_read(
        NV_ITEM_RELAY_CLUSTER_DATA(cluster->relay_idx),
        sizeof(zigbee_relay_cluster_config), (uint8_t *)&nv_config_buffer);

    if (st != HAL_NVM_SUCCESS)
        return;

    cluster->startup_mode       = nv_config_buffer.startup_mode;
    cluster->indicator_led_mode = nv_config_buffer.indicator_led_mode;
    cluster->indicator_state    = nv_config_buffer.indicator_led_on;
    cluster->inching_ms         = nv_config_buffer.inching_ms;
    cluster->interlock_group    = nv_config_buffer.interlock_group;
}

void relay_cluster_handle_startup_mode(zigbee_relay_cluster *cluster) {
    hal_nvm_status_t st = hal_nvm_read(
        NV_ITEM_RELAY_CLUSTER_DATA(cluster->relay_idx),
        sizeof(zigbee_relay_cluster_config), (uint8_t *)&nv_config_buffer);

    if (st != HAL_NVM_SUCCESS)
        return;

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
