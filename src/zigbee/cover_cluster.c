#include "cover_cluster.h"

#include "base_components/cover_controller.h"
#include "cluster_common.h"
#include "zcl_cluster_defs.h"
#include "zcl_ids.h"
#include "zcl_types.h"

static zigbee_cover_cluster *cover_cluster_by_endpoint[HAL_ZIGBEE_ENDPOINT_ID_MAX + 1];
static uint8_t window_covering_type;

static zigbee_cover_cluster *cover_cluster_find(uint8_t endpoint) {
    return endpoint <= HAL_ZIGBEE_ENDPOINT_ID_MAX ? cover_cluster_by_endpoint[endpoint] : NULL;
}

static uint8_t cover_motion_to_zigbee(cover_motion_t motion) {
    return motion == COVER_OPENING ? ZCL_ATTR_WINDOW_COVERING_MOVING_OPENING :
           motion == COVER_CLOSING ? ZCL_ATTR_WINDOW_COVERING_MOVING_CLOSING :
           ZCL_ATTR_WINDOW_COVERING_MOVING_STOPPED;
}

static void cover_cluster_notify(zigbee_cover_cluster *cluster, uint16_t attribute_id) {
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_WINDOW_COVERING, attribute_id);
}

static void cover_cluster_sync(zigbee_cover_cluster *cluster, bool notify) {
    cover_state_t state;
    uint8_t       moving;
    uint8_t       position;

    if (!cover_controller_state(cluster->controller_id, &state)) return;

    moving   = cover_motion_to_zigbee(state.motion);
    position = cover_controller_lift_percentage(cluster->controller_id);
    if (cluster->moving != moving) {
        cluster->moving = moving;
        if (notify) cover_cluster_notify(cluster, ZCL_ATTR_WINDOW_COVERING_MOVING);
    }
    if (cluster->position != position) {
        cluster->position = position;
        if (notify)
            cover_cluster_notify(cluster,
                                 ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE);
    }
    if (cluster->motor_reversal != state.motor_reversal) {
        cluster->motor_reversal = state.motor_reversal;
        if (notify) cover_cluster_notify(cluster, ZCL_ATTR_WINDOW_COVERING_MOTOR_REVERSAL);
    }
    if (cluster->open_time_ms != state.open_time_ms) {
        cluster->open_time_ms = state.open_time_ms;
        if (notify) cover_cluster_notify(cluster, ZCL_ATTR_WINDOW_COVERING_OPEN_TIME);
    }
    if (cluster->close_time_ms != state.close_time_ms) {
        cluster->close_time_ms = state.close_time_ms;
        if (notify) cover_cluster_notify(cluster, ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME);
    }
    if (cluster->closed_deadzone_bp != state.closed_deadzone_bp) {
        cluster->closed_deadzone_bp = state.closed_deadzone_bp;
        if (notify)
            cover_cluster_notify(cluster, ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE);
    }
    if (cluster->open_deadzone_bp != state.open_deadzone_bp) {
        cluster->open_deadzone_bp = state.open_deadzone_bp;
        if (notify) cover_cluster_notify(cluster, ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE);
    }
    if (cluster->overrun_ms != state.overrun_ms) {
        cluster->overrun_ms = state.overrun_ms;
        if (notify) cover_cluster_notify(cluster, ZCL_ATTR_WINDOW_COVERING_OVERRUN);
    }
}

static void cover_cluster_on_state_changed(void *context, uint8_t cover_id,
                                           const cover_state_t *state) {
    zigbee_cover_cluster *cluster = (zigbee_cover_cluster *)context;

    (void)cover_id;
    (void)state;
    cover_cluster_sync(cluster, true);
}

static hal_zigbee_cmd_result_t cover_cluster_attr_write(
    uint8_t endpoint, uint16_t attribute_id, const uint8_t *candidate,
    uint16_t candidate_len, hal_zigbee_attr_write_phase_t phase) {
    zigbee_cover_cluster *cluster = cover_cluster_find(endpoint);

    if (cluster == NULL || candidate == NULL) return HAL_ZIGBEE_CMD_SKIPPED;

    for (uint8_t i = 0; i < sizeof(cluster->attr_infos) / sizeof(cluster->attr_infos[0]); i++) {
        if (cluster->attr_infos[i].attribute_id != attribute_id) continue;
        if (candidate_len != cluster->attr_infos[i].size) return HAL_ZIGBEE_INVALID_VALUE;

        if (phase != HAL_ZIGBEE_ATTR_COMMIT) return HAL_ZIGBEE_CMD_PROCESSED;

        if (attribute_id == ZCL_ATTR_WINDOW_COVERING_MOTOR_REVERSAL) {
            (void)cover_controller_set_motor_reversal(cluster->controller_id,
                                                      cluster->motor_reversal != 0);
        } else if (attribute_id == ZCL_ATTR_WINDOW_COVERING_OPEN_TIME) {
            (void)cover_controller_set_open_time(cluster->controller_id, cluster->open_time_ms);
        } else if (attribute_id == ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME) {
            (void)cover_controller_set_close_time(cluster->controller_id, cluster->close_time_ms);
        } else if (attribute_id == ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE) {
            (void)cover_controller_set_closed_deadzone(cluster->controller_id,
                                                       cluster->closed_deadzone_bp);
        } else if (attribute_id == ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE) {
            (void)cover_controller_set_open_deadzone(cluster->controller_id,
                                                     cluster->open_deadzone_bp);
        } else if (attribute_id == ZCL_ATTR_WINDOW_COVERING_OVERRUN) {
            (void)cover_controller_set_overrun(cluster->controller_id, cluster->overrun_ms);
        }
        cover_cluster_sync(cluster, false);
        return HAL_ZIGBEE_CMD_PROCESSED;
    }
    return HAL_ZIGBEE_CMD_SKIPPED;
}

static hal_zigbee_cmd_result_t cover_cluster_callback(zigbee_cover_cluster *cluster,
                                                      uint8_t command_id,
                                                      void *payload,
                                                      uint16_t payload_len) {
    cover_result_t result;

    if (command_id == ZCL_CMD_WINDOW_COVERING_UP_OPEN) {
        result = cover_open(cluster->controller_id);
    } else if (command_id == ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE) {
        result = cover_close(cluster->controller_id);
    } else if (command_id == ZCL_CMD_WINDOW_COVERING_STOP) {
        result = cover_stop(cluster->controller_id);
    } else if (command_id == ZCL_CMD_WINDOW_COVERING_GOTO_LIFT_PERCENTAGE) {
        uint8_t percentage;

        if (payload_len != sizeof(percentage) || payload == NULL)
            return HAL_ZIGBEE_MALFORMED_COMMAND;

        percentage = *(uint8_t *)payload;
        if (percentage > 100) return HAL_ZIGBEE_INVALID_VALUE;

        result = cover_goto(cluster->controller_id,
                            (uint16_t)(100 - percentage) * 100);
        if (result == COVER_RESULT_BLOCKED) return HAL_ZIGBEE_ACTION_DENIED;
    } else {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    return result == COVER_RESULT_INVALID ? HAL_ZIGBEE_CMD_SKIPPED :
           HAL_ZIGBEE_CMD_PROCESSED;
}

static hal_zigbee_cmd_result_t cover_cluster_callback_trampoline(
    const hal_zigbee_incoming_command_t *command) {
    if (command == NULL) return HAL_ZIGBEE_CMD_SKIPPED;

    zigbee_cover_cluster *cluster = cover_cluster_find(command->destination_endpoint);

    return cluster == NULL ? HAL_ZIGBEE_CMD_SKIPPED :
           cover_cluster_callback(cluster, command->command_id,
                                  (void *)command->payload, command->payload_len);
}

void cover_cluster_add_to_endpoint(zigbee_cover_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    if (!hal_zigbee_endpoint_reserve_clusters(endpoint, endpoint->cluster_capacity, 1) ||
        !cover_controller_subscribe(cluster->controller_id,
                                    cover_cluster_on_state_changed, cluster)) return;

    cover_cluster_by_endpoint[endpoint->endpoint] = cluster;
    cluster->endpoint = endpoint->endpoint;
    cluster->position = 0xff;
    cover_cluster_sync(cluster, false);

    SETUP_ATTR(0, ZCL_ATTR_WINDOW_COVERING_TYPE, ZCL_DATA_TYPE_ENUM8, HAL_ATTR_READ,
               window_covering_type);
    SETUP_ATTR(1, ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE,
               ZCL_DATA_TYPE_UINT8, HAL_ATTR_READ, cluster->position);
    SETUP_ATTR(2, ZCL_ATTR_WINDOW_COVERING_MOVING, ZCL_DATA_TYPE_ENUM8, HAL_ATTR_READ,
               cluster->moving);
    SETUP_ATTR(3, ZCL_ATTR_WINDOW_COVERING_MOTOR_REVERSAL, ZCL_DATA_TYPE_BOOLEAN,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->motor_reversal);
    SETUP_ATTR(4, ZCL_ATTR_WINDOW_COVERING_OPEN_TIME, ZCL_DATA_TYPE_UINT32,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->open_time_ms);
    SETUP_ATTR(5, ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME, ZCL_DATA_TYPE_UINT32,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->close_time_ms);
    SETUP_ATTR(6, ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE, ZCL_DATA_TYPE_UINT16,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->closed_deadzone_bp);
    SETUP_ATTR(7, ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE, ZCL_DATA_TYPE_UINT16,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->open_deadzone_bp);
    SETUP_ATTR(8, ZCL_ATTR_WINDOW_COVERING_OVERRUN, ZCL_DATA_TYPE_UINT16,
               (HAL_ATTR_READ | HAL_ATTR_WRITE), cluster->overrun_ms);

    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id          = ZCL_CLUSTER_WINDOW_COVERING,
        .role                = HAL_ZIGBEE_ROLE_SERVER,
        .attribute_count     =                                 9,.attributes= cluster->attr_infos,
        .cmd_callback        = cover_cluster_callback_trampoline,
        .attr_write_callback = cover_cluster_attr_write,
    };

    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity, &endpoint_cluster);
}
