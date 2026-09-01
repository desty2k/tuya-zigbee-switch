#include "relay_controller.h"

#include "base_components/interlock.h"
#include <stddef.h>

typedef struct {
    relay_state_cb_t callback;
    void *           context;
} relay_observer_t;

typedef struct {
    bool              is_on;
    bool              state_applied;
    uint8_t           relay_id;
    uint16_t          inhibit_mask;
    auto_off_reason_t auto_off_reason;
    auto_off_reason_t deferred_auto_off_reason;
    uint32_t          deferred_auto_off_ms;
    bool              deferred_on;
    app_timer_t       auto_off_timer;
    relay_config_t *  config;
    relay_driver_t *  driver;
    relay_observer_t  observers[RELAY_CONTROLLER_MAX_OBSERVERS];
} relay_runtime_t;

static relay_runtime_t relay_ctrl_runtimes[RELAY_CONTROLLER_MAX_RELAYS];
static uint8_t         relay_ctrl_relay_count;

void relay_ctrl_init(void) {
    relay_ctrl_relay_count = 0;
    interlock_init();
}

static void relay_ctrl_auto_off(void *arg) {
    relay_runtime_t *runtime = (relay_runtime_t *)arg;

    (void)relay_ctrl_submit(&(relay_request_t){
        .relay_id = runtime->relay_id,
        .type     = RELAY_REQUEST_OFF,
        .source   = RELAY_SOURCE_TIMER,
    });
}

static void relay_ctrl_notify(relay_runtime_t *runtime, bool is_on,
                              relay_request_source_t source) {
    for (uint8_t i = 0; i < RELAY_CONTROLLER_MAX_OBSERVERS; i++) {
        if (runtime->observers[i].callback != NULL) {
            runtime->observers[i].callback(runtime->observers[i].context,
                                           runtime->relay_id, is_on, source);
        }
    }
}

uint8_t relay_ctrl_add(relay_driver_t *driver, relay_config_t *config) {
    uint8_t relay_id = relay_ctrl_relay_count;

    if (driver == NULL || config == NULL ||
        relay_ctrl_relay_count >= RELAY_CONTROLLER_MAX_RELAYS) {
        return UINT8_MAX;
    }

    relay_ctrl_runtimes[relay_id].is_on                    = false;
    relay_ctrl_runtimes[relay_id].state_applied            = false;
    relay_ctrl_runtimes[relay_id].relay_id                 = relay_id;
    relay_ctrl_runtimes[relay_id].inhibit_mask             = 0;
    relay_ctrl_runtimes[relay_id].auto_off_reason          = AUTO_OFF_NONE;
    relay_ctrl_runtimes[relay_id].deferred_auto_off_reason = AUTO_OFF_NONE;
    relay_ctrl_runtimes[relay_id].deferred_auto_off_ms     = 0;
    relay_ctrl_runtimes[relay_id].deferred_on              = false;
    relay_ctrl_runtimes[relay_id].config                   = config;
    relay_ctrl_runtimes[relay_id].driver                   = driver;
    for (uint8_t i = 0; i < RELAY_CONTROLLER_MAX_OBSERVERS; i++) {
        relay_ctrl_runtimes[relay_id].observers[i].callback = NULL;
        relay_ctrl_runtimes[relay_id].observers[i].context  = NULL;
    }
    timer_init(&relay_ctrl_runtimes[relay_id].auto_off_timer,
               relay_ctrl_auto_off, &relay_ctrl_runtimes[relay_id]);
    relay_driver_init(driver);
    relay_ctrl_relay_count++;
    return relay_id;
}

bool relay_ctrl_subscribe(uint8_t relay_id, relay_state_cb_t callback,
                          void *context) {
    relay_runtime_t *runtime;

    if (relay_id >= relay_ctrl_relay_count || callback == NULL) {
        return false;
    }
    runtime = &relay_ctrl_runtimes[relay_id];
    for (uint8_t i = 0; i < RELAY_CONTROLLER_MAX_OBSERVERS; i++) {
        if (runtime->observers[i].callback == callback &&
            runtime->observers[i].context == context) {
            return true;
        }
        if (runtime->observers[i].callback == NULL) {
            runtime->observers[i].callback = callback;
            runtime->observers[i].context  = context;
            return true;
        }
    }
    return false;
}

relay_result_t relay_ctrl_submit(const relay_request_t *request) {
    relay_runtime_t * runtime;
    bool              target_on;
    uint32_t          auto_off_ms     = 0;
    auto_off_reason_t auto_off_reason = AUTO_OFF_NONE;

    if (request == NULL || request->relay_id >= relay_ctrl_relay_count) {
        return RELAY_RESULT_INVALID;
    }
    runtime = &relay_ctrl_runtimes[request->relay_id];
    if (request->type == RELAY_REQUEST_TOGGLE) {
        target_on = !runtime->is_on;
    } else if (request->type == RELAY_REQUEST_ON) {
        target_on = true;
    } else if (request->type == RELAY_REQUEST_OFF) {
        target_on = false;
    } else if (request->type == RELAY_REQUEST_PULSE) {
        auto_off_ms = request->duration_ms != 0 ? request->duration_ms
                                                : runtime->config->inching_ms;
        if (auto_off_ms == 0) {
            return RELAY_RESULT_INVALID;
        }
        target_on       = true;
        auto_off_reason = AUTO_OFF_PULSE;
    } else if (request->type == RELAY_REQUEST_ON_TIMED) {
        target_on       = true;
        auto_off_ms     = request->duration_ms;
        auto_off_reason = AUTO_OFF_TIMED;
    } else {
        return RELAY_RESULT_INVALID;
    }

    if (target_on && runtime->inhibit_mask != 0) {
        return RELAY_RESULT_BLOCKED;
    }
    if (request->source == RELAY_SOURCE_INTERLOCK && runtime->deferred_on) {
        auto_off_reason      = runtime->deferred_auto_off_reason;
        auto_off_ms          = runtime->deferred_auto_off_ms;
        runtime->deferred_on = false;
    } else if (request->source != RELAY_SOURCE_INTERLOCK) {
        runtime->deferred_on = false;
    }

    timer_cancel(&runtime->auto_off_timer);
    runtime->auto_off_reason = auto_off_reason;
    if (runtime->state_applied && runtime->is_on == target_on) {
        if (auto_off_reason != AUTO_OFF_NONE) {
            timer_restart(&runtime->auto_off_timer, auto_off_ms);
        }
        return RELAY_RESULT_UNCHANGED;
    }

    if (!target_on) {
        runtime->deferred_on = false;
        interlock_cancel_pending(request->relay_id);
    } else if (!interlock_prepare(request->relay_id)) {
        runtime->deferred_auto_off_reason = auto_off_reason;
        runtime->deferred_auto_off_ms     = auto_off_ms;
        runtime->deferred_on = true;
        return RELAY_RESULT_DEFERRED;
    }
    if (auto_off_reason != AUTO_OFF_NONE) {
        timer_restart(&runtime->auto_off_timer, auto_off_ms);
    }

    bool state_changed = runtime->is_on != target_on;
    runtime->is_on         = target_on;
    runtime->state_applied = true;
    relay_driver_apply(runtime->driver, target_on);
    if (!target_on && state_changed) {
        interlock_note_off(request->relay_id);
    }
    if (state_changed) {
        relay_ctrl_notify(runtime, target_on, request->source);
    }
    return RELAY_RESULT_APPLIED;
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

void relay_ctrl_set_interlock_group(uint8_t relay_id, uint8_t group_id) {
    if (relay_id < relay_ctrl_relay_count) {
        relay_ctrl_runtimes[relay_id].config->interlock_group = group_id;
        interlock_set_relay_group(relay_id, group_id);
    }
}

uint8_t relay_ctrl_get_interlock_group(uint8_t relay_id) {
    return relay_id < relay_ctrl_relay_count
           ? relay_ctrl_runtimes[relay_id].config->interlock_group
           : INTERLOCK_NO_GROUP;
}

auto_off_reason_t relay_ctrl_auto_off_reason(uint8_t relay_id) {
    return relay_id < relay_ctrl_relay_count
           ? relay_ctrl_runtimes[relay_id].auto_off_reason
           : AUTO_OFF_NONE;
}

void relay_ctrl_cancel_deferred_on(uint8_t relay_id) {
    if (relay_id < relay_ctrl_relay_count) {
        relay_ctrl_runtimes[relay_id].deferred_on = false;
    }
}

bool relay_ctrl_inhibit(uint16_t relay_mask, uint8_t inhibit_id) {
    bool     changed = false;
    uint16_t bit;

    if (inhibit_id >= RELAY_CONTROLLER_MAX_INHIBITORS) {
        return false;
    }
    bit = (uint16_t)1u << inhibit_id;
    for (uint8_t relay_id = 0; relay_id < relay_ctrl_relay_count; relay_id++) {
        if ((relay_mask & ((uint16_t)1u << relay_id)) != 0) {
            relay_ctrl_runtimes[relay_id].inhibit_mask |= bit;
            changed = true;
        }
    }
    return changed;
}

bool relay_ctrl_release(uint16_t relay_mask, uint8_t inhibit_id) {
    bool     changed = false;
    uint16_t bit;

    if (inhibit_id >= RELAY_CONTROLLER_MAX_INHIBITORS) {
        return false;
    }
    bit = (uint16_t)1u << inhibit_id;
    for (uint8_t relay_id = 0; relay_id < relay_ctrl_relay_count; relay_id++) {
        if ((relay_mask & ((uint16_t)1u << relay_id)) != 0) {
            relay_ctrl_runtimes[relay_id].inhibit_mask &= (uint16_t) ~bit;
            changed = true;
        }
    }
    return changed;
}
