#include "relay_controller.h"

#include <stddef.h>

typedef struct {
    bool              is_on;
    bool              state_applied;
    uint8_t           relay_id;
    auto_off_reason_t auto_off_reason;
    app_timer_t       auto_off_timer;
    relay_config_t *  config;
    relay_driver_t *  driver;
    relay_state_cb_t  on_change;
    void *            cb_param;
} relay_runtime_t;

static relay_runtime_t relay_ctrl_runtimes[RELAY_CONTROLLER_MAX_RELAYS];
static uint8_t         relay_ctrl_relay_count;

void relay_ctrl_init(void) {
    relay_ctrl_relay_count = 0;
}

static void relay_ctrl_auto_off(void *arg) {
    relay_runtime_t *runtime = (relay_runtime_t *)arg;
    relay_request_t  request = {
        .relay_id = runtime->relay_id,
        .type     = RELAY_REQUEST_OFF,
        .source   = RELAY_SOURCE_TIMER,
    };

    relay_ctrl_submit(&request);
}

uint8_t relay_ctrl_add(relay_driver_t *driver, relay_config_t *config) {
    uint8_t relay_id = relay_ctrl_relay_count;

    if (driver == NULL || config == NULL ||
        relay_ctrl_relay_count >= RELAY_CONTROLLER_MAX_RELAYS) {
        return UINT8_MAX;
    }

    relay_ctrl_runtimes[relay_id].is_on           = false;
    relay_ctrl_runtimes[relay_id].state_applied   = false;
    relay_ctrl_runtimes[relay_id].relay_id        = relay_id;
    relay_ctrl_runtimes[relay_id].auto_off_reason = AUTO_OFF_NONE;
    relay_ctrl_runtimes[relay_id].config          = config;
    relay_ctrl_runtimes[relay_id].driver          = driver;
    relay_ctrl_runtimes[relay_id].on_change       = NULL;
    relay_ctrl_runtimes[relay_id].cb_param        = NULL;
    timer_init(&relay_ctrl_runtimes[relay_id].auto_off_timer,
               relay_ctrl_auto_off, &relay_ctrl_runtimes[relay_id]);
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
    relay_runtime_t * runtime;
    bool              target_on;
    uint32_t          auto_off_ms     = 0;
    auto_off_reason_t auto_off_reason = AUTO_OFF_NONE;

    if (request == NULL || request->relay_id >= relay_ctrl_relay_count) {
        return HAL_ZIGBEE_INVALID_VALUE;
    }
    runtime = &relay_ctrl_runtimes[request->relay_id];
    if (request->type == RELAY_REQUEST_TOGGLE) {
        target_on = !relay_ctrl_runtimes[request->relay_id].is_on;
    } else if (request->type == RELAY_REQUEST_ON) {
        target_on = true;
    } else if (request->type == RELAY_REQUEST_OFF) {
        target_on = false;
    } else if (request->type == RELAY_REQUEST_PULSE) {
        auto_off_ms = request->duration_ms != 0 ? request->duration_ms
                                                : runtime->config->inching_ms;
        if (auto_off_ms == 0) {
            return HAL_ZIGBEE_INVALID_VALUE;
        }
        target_on       = true;
        auto_off_reason = AUTO_OFF_PULSE;
    } else if (request->type == RELAY_REQUEST_ON_TIMED) {
        target_on       = true;
        auto_off_ms     = request->duration_ms;
        auto_off_reason = AUTO_OFF_TIMED;
    } else {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }

    timer_cancel(&runtime->auto_off_timer);
    runtime->auto_off_reason = auto_off_reason;
    if (auto_off_reason != AUTO_OFF_NONE) {
        timer_restart(&runtime->auto_off_timer, auto_off_ms);
    }

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

void relay_ctrl_set_inching_ms(uint8_t relay_id, uint16_t inching_ms) {
    if (relay_id < relay_ctrl_relay_count) {
        relay_ctrl_runtimes[relay_id].config->inching_ms = inching_ms;
    }
}

uint16_t relay_ctrl_get_inching_ms(uint8_t relay_id) {
    return relay_id < relay_ctrl_relay_count
           ? relay_ctrl_runtimes[relay_id].config->inching_ms
           : 0;
}

auto_off_reason_t relay_ctrl_auto_off_reason(uint8_t relay_id) {
    return relay_id < relay_ctrl_relay_count
           ? relay_ctrl_runtimes[relay_id].auto_off_reason
           : AUTO_OFF_NONE;
}
