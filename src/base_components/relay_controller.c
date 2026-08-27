#include "relay_controller.h"

#include <stddef.h>

typedef struct {
    bool             is_on;
    bool             state_applied;
    relay_driver_t * driver;
    relay_state_cb_t on_change;
    void *           cb_param;
} relay_runtime_t;

static relay_runtime_t relay_ctrl_runtimes[RELAY_CONTROLLER_MAX_RELAYS];
static uint8_t         relay_ctrl_relay_count;

void relay_ctrl_init(void) {
    relay_ctrl_relay_count = 0;
}

uint8_t relay_ctrl_add(relay_driver_t *driver) {
    uint8_t relay_id = relay_ctrl_relay_count;

    if (driver == NULL || relay_ctrl_relay_count >= RELAY_CONTROLLER_MAX_RELAYS) {
        return UINT8_MAX;
    }

    relay_ctrl_runtimes[relay_id].is_on         = false;
    relay_ctrl_runtimes[relay_id].state_applied = false;
    relay_ctrl_runtimes[relay_id].driver        = driver;
    relay_ctrl_runtimes[relay_id].on_change     = NULL;
    relay_ctrl_runtimes[relay_id].cb_param      = NULL;
    relay_driver_init(driver);
    relay_ctrl_relay_count++;
    return relay_id;
}

void relay_ctrl_set_state_callback(uint8_t relay_id, relay_state_cb_t callback,
                                   void *param) {
    if (relay_id >= relay_ctrl_relay_count) {
        return;
    }
    relay_ctrl_runtimes[relay_id].on_change = callback;
    relay_ctrl_runtimes[relay_id].cb_param  = param;
}

hal_zigbee_cmd_result_t relay_ctrl_submit(const relay_request_t *request) {
    relay_runtime_t *runtime;
    bool             target_on;

    if (request == NULL || request->relay_id >= relay_ctrl_relay_count) {
        return HAL_ZIGBEE_INVALID_VALUE;
    }
    if (request->type == RELAY_REQUEST_TOGGLE) {
        target_on = !relay_ctrl_runtimes[request->relay_id].is_on;
    } else if (request->type == RELAY_REQUEST_ON) {
        target_on = true;
    } else if (request->type == RELAY_REQUEST_OFF) {
        target_on = false;
    } else {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }

    runtime = &relay_ctrl_runtimes[request->relay_id];
    if (runtime->state_applied && runtime->is_on == target_on) {
        return HAL_ZIGBEE_CMD_PROCESSED;
    }

    bool state_changed = runtime->is_on != target_on;
    runtime->is_on         = target_on;
    runtime->state_applied = true;
    relay_driver_apply(runtime->driver, target_on);
    if (state_changed && runtime->on_change != NULL) {
        runtime->on_change(runtime->cb_param, request->relay_id, target_on);
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}

bool relay_ctrl_is_on(uint8_t relay_id) {
    return relay_id < relay_ctrl_relay_count &&
           relay_ctrl_runtimes[relay_id].is_on;
}

uint8_t relay_ctrl_count(void) {
    return relay_ctrl_relay_count;
}
