#include "cover_controller.h"

#include "base_components/relay_controller.h"
#include "base_components/timer_service.h"
#include "hal/nvm.h"
#include "hal/timer.h"
#include <stddef.h>

#define COVER_STATE_VERSION    2

typedef struct {
    cover_state_cb_t callback;
    void *           context;
} cover_observer_t;

typedef struct {
    cover_controller_config_t config;
    cover_state_t             state;
    uint32_t                  movement_started_ms;
    uint16_t                  movement_started_bp;
    bool                      endpoint_target;
    bool                      prepared;
    bool                      stopping;
    uint8_t                   cover_id;
    app_timer_t               safety_timer;
    app_timer_t               movement_timer;
    app_timer_t               position_timer;
    cover_observer_t          observers[COVER_CONTROLLER_MAX_OBSERVERS];
} cover_runtime_t;

static cover_runtime_t cover_runtimes[COVER_CONTROLLER_MAX_COVERS];
static uint8_t         cover_count;

static uint16_t cover_open_position(const cover_state_t *state) {
    uint32_t usable;

    if (state->travel_position_bp <= state->closed_deadzone_bp) return 0;

    if (state->travel_position_bp >= COVER_POSITION_MAX_BP - state->open_deadzone_bp)
        return COVER_POSITION_MAX_BP;

    usable = COVER_POSITION_MAX_BP - state->closed_deadzone_bp - state->open_deadzone_bp;
    return (uint16_t)(((uint32_t)(state->travel_position_bp - state->closed_deadzone_bp) *
                       COVER_POSITION_MAX_BP) / usable);
}

static void cover_update_open_position(cover_runtime_t *runtime) {
    runtime->state.open_position_bp = cover_open_position(&runtime->state);
}

static void cover_notify(const cover_runtime_t *runtime) {
    for (uint8_t i = 0; i < COVER_CONTROLLER_MAX_OBSERVERS; i++) {
        if (runtime->observers[i].callback != NULL)
            runtime->observers[i].callback(runtime->observers[i].context,
                                           runtime->cover_id, &runtime->state);
    }
}

static void cover_changed(cover_runtime_t *runtime, cover_state_t before) {
    if (before.motion != runtime->state.motion ||
        before.confidence != runtime->state.confidence ||
        before.travel_position_bp != runtime->state.travel_position_bp ||
        before.open_position_bp != runtime->state.open_position_bp ||
        before.target_travel_bp != runtime->state.target_travel_bp ||
        before.open_time_ms != runtime->state.open_time_ms ||
        before.close_time_ms != runtime->state.close_time_ms ||
        before.closed_deadzone_bp != runtime->state.closed_deadzone_bp ||
        before.open_deadzone_bp != runtime->state.open_deadzone_bp ||
        before.overrun_ms != runtime->state.overrun_ms ||
        before.motor_reversal != runtime->state.motor_reversal) cover_notify(runtime);
}

static uint8_t cover_open_relay(const cover_runtime_t *runtime) {
    return runtime->state.motor_reversal ? runtime->config.close_relay_id :
           runtime->config.open_relay_id;
}

static uint8_t cover_close_relay(const cover_runtime_t *runtime) {
    return runtime->state.motor_reversal ? runtime->config.open_relay_id :
           runtime->config.close_relay_id;
}

static cover_result_t cover_result_from_relay(relay_result_t result) {
    switch (result) {
    case RELAY_RESULT_APPLIED: return COVER_RESULT_APPLIED;

    case RELAY_RESULT_UNCHANGED: return COVER_RESULT_UNCHANGED;

    case RELAY_RESULT_DEFERRED: return COVER_RESULT_DEFERRED;

    case RELAY_RESULT_BLOCKED: return COVER_RESULT_BLOCKED;

    default: return COVER_RESULT_INVALID;
    }
}

static bool cover_is_manual(const cover_runtime_t *runtime) {
    return runtime->state.open_time_ms == 0 && runtime->state.close_time_ms == 0;
}

static uint32_t cover_effective_time(const cover_runtime_t *runtime,
                                     cover_motion_t direction) {
    uint32_t selected = direction == COVER_OPENING ? runtime->state.open_time_ms :
                        runtime->state.close_time_ms;
    uint32_t peer = direction == COVER_OPENING ? runtime->state.close_time_ms :
                    runtime->state.open_time_ms;

    return selected != 0 ? selected : peer;
}

static bool cover_store(const cover_runtime_t *runtime, bool motion_active) {
    cover_state_record_t record = {
        .version            = COVER_STATE_VERSION,
        .motor_reversal     = runtime->state.motor_reversal,
        .motion_active      = motion_active,
        .confidence         = runtime->state.confidence,
        .open_time_ms       = runtime->state.open_time_ms,
        .close_time_ms      = runtime->state.close_time_ms,
        .closed_deadzone_bp = runtime->state.closed_deadzone_bp,
        .open_deadzone_bp   = runtime->state.open_deadzone_bp,
        .overrun_ms         = runtime->state.overrun_ms,
        .travel_position_bp = runtime->state.travel_position_bp,
    };

    return hal_nvm_write(runtime->config.nvm_item, sizeof(record),
                         (uint8_t *)&record) == HAL_NVM_SUCCESS;
}

static void cover_stop_relays(const cover_runtime_t *runtime) {
    (void)relay_ctrl_submit(&(relay_request_t){ .relay_id = runtime->config.open_relay_id,
                                                .type     = RELAY_REQUEST_OFF,
                                                .source   = RELAY_SOURCE_COVER });
    (void)relay_ctrl_submit(&(relay_request_t){ .relay_id = runtime->config.close_relay_id,
                                                .type     = RELAY_REQUEST_OFF,
                                                .source   = RELAY_SOURCE_COVER });
}

static void cover_cancel_timers(cover_runtime_t *runtime) {
    timer_cancel(&runtime->safety_timer);
    timer_cancel(&runtime->movement_timer);
    timer_cancel(&runtime->position_timer);
}

static void cover_update_position(cover_runtime_t *runtime) {
    uint32_t      travel_time;
    uint32_t      elapsed;
    uint32_t      delta;
    uint16_t      position;
    cover_state_t before = runtime->state;

    if (runtime->state.motion == COVER_STOPPED || cover_is_manual(runtime)) return;

    travel_time = cover_effective_time(runtime, runtime->state.motion);
    elapsed     = hal_millis() - runtime->movement_started_ms;

    /* elapsed < travel_time <= COVER_MAX_TRAVEL_TIME_MS keeps the product
     * within 32 bits; the overrun phase runs past travel_time. */
    delta = elapsed >= travel_time ? COVER_POSITION_MAX_BP :
            (elapsed * COVER_POSITION_MAX_BP) / travel_time;
    if (runtime->state.motion == COVER_OPENING) {
        uint32_t candidate = (uint32_t)runtime->movement_started_bp + delta;
        position = candidate >= runtime->state.target_travel_bp ?
                   runtime->state.target_travel_bp : (uint16_t)candidate;
    } else {
        position = delta >= runtime->movement_started_bp ||
                   runtime->movement_started_bp - delta <= runtime->state.target_travel_bp ?
                   runtime->state.target_travel_bp :
                   (uint16_t)(runtime->movement_started_bp - delta);
    }
    runtime->state.travel_position_bp = position;
    cover_update_open_position(runtime);
    cover_changed(runtime, before);
}

/* Only a completed endpoint move turns an UNKNOWN position into a known one. */
static void cover_finish(cover_runtime_t *runtime,
                         cover_position_confidence_t confidence) {
    cover_state_t before = runtime->state;

    if (before.confidence == COVER_POSITION_UNKNOWN && confidence != COVER_POSITION_KNOWN)
        confidence = COVER_POSITION_UNKNOWN;
    cover_update_position(runtime);
    cover_cancel_timers(runtime);
    runtime->prepared         = false;
    runtime->stopping         = true;
    runtime->state.motion     = COVER_STOPPED;
    runtime->state.confidence = confidence;
    cover_update_open_position(runtime);
    cover_stop_relays(runtime);
    runtime->stopping = false;
    if (!cover_store(runtime, false)) runtime->state.confidence = COVER_POSITION_UNKNOWN;
    cover_changed(runtime, before);
}

static uint32_t cover_movement_duration_ms(const cover_runtime_t *runtime,
                                           cover_motion_t direction) {
    uint16_t distance = runtime->state.target_travel_bp >= runtime->movement_started_bp ?
                        runtime->state.target_travel_bp - runtime->movement_started_bp :
                        runtime->movement_started_bp - runtime->state.target_travel_bp;
    uint32_t duration = ((uint32_t)distance * cover_effective_time(runtime, direction)) /
                        COVER_POSITION_MAX_BP;

    if (runtime->endpoint_target) duration += runtime->state.overrun_ms;
    return duration;
}

static void cover_movement_elapsed(void *arg) {
    cover_runtime_t *runtime = (cover_runtime_t *)arg;

    if (runtime->state.motion == COVER_STOPPED) return;

    runtime->state.travel_position_bp = runtime->state.target_travel_bp;
    cover_update_open_position(runtime);
    cover_finish(runtime, runtime->endpoint_target ? COVER_POSITION_KNOWN :
                 COVER_POSITION_ESTIMATED);
}

static void cover_position_elapsed(void *arg) {
    cover_runtime_t *runtime = (cover_runtime_t *)arg;

    cover_update_position(runtime);
    if (runtime->state.motion != COVER_STOPPED)
        timer_restart(&runtime->position_timer, COVER_POSITION_UPDATE_MS);
}

static void cover_safety_timeout(void *arg) {
    cover_finish((cover_runtime_t *)arg, COVER_POSITION_UNKNOWN);
}

static cover_position_confidence_t cover_stop_confidence(const cover_runtime_t *runtime) {
    return cover_is_manual(runtime) ? COVER_POSITION_UNKNOWN : COVER_POSITION_ESTIMATED;
}

/* A second attempt covers a transiently full task pool. */
static bool cover_arm(app_timer_t *timer, uint32_t delay_ms) {
    return timer_restart(timer, delay_ms) || timer_restart(timer, delay_ms);
}

/* Arms the timer that stops the motor; false when none could be armed. */
static bool cover_arm_cutoff(cover_runtime_t *runtime, cover_motion_t direction) {
    if (cover_is_manual(runtime))
        return cover_arm(&runtime->safety_timer, runtime->config.manual_max_run_ms);

    if (!cover_arm(&runtime->movement_timer, cover_movement_duration_ms(runtime, direction)))
        return false;

    timer_restart(&runtime->position_timer, COVER_POSITION_UPDATE_MS);
    return true;
}

static void cover_start_applied(cover_runtime_t *runtime, cover_motion_t direction) {
    cover_state_t before = runtime->state;

    runtime->state.motion        = direction;
    runtime->movement_started_ms = hal_millis();
    if (!runtime->prepared) {
        runtime->prepared               = true;
        runtime->endpoint_target        = true;
        runtime->state.target_travel_bp = direction == COVER_OPENING ?
                                          COVER_POSITION_MAX_BP : 0;
    }

    /* From an unknown position an endpoint move times the full travel, as if
     * starting at the opposite endpoint. */
    if (runtime->endpoint_target && runtime->state.confidence == COVER_POSITION_UNKNOWN)
        runtime->state.travel_position_bp = direction == COVER_OPENING ? 0 :
                                            COVER_POSITION_MAX_BP;
    runtime->movement_started_bp = runtime->state.travel_position_bp;
    cover_update_open_position(runtime);
    if (!cover_arm_cutoff(runtime, direction)) {
        cover_finish(runtime, cover_stop_confidence(runtime));
        return;
    }
    cover_changed(runtime, before);
}

static void cover_relay_changed(void *context, uint8_t relay_id, bool is_on,
                                relay_request_source_t source) {
    cover_runtime_t *runtime = (cover_runtime_t *)context;
    uint8_t          open_relay;
    uint8_t          close_relay;

    (void)relay_id;
    (void)is_on;
    (void)source;
    if (runtime->stopping) return;

    open_relay  = cover_open_relay(runtime);
    close_relay = cover_close_relay(runtime);
    if (relay_ctrl_is_on(open_relay) && relay_ctrl_is_on(close_relay)) {
        cover_cancel_timers(runtime);
        runtime->prepared         = false;
        runtime->state.confidence = COVER_POSITION_UNKNOWN;
        runtime->state.motion     = COVER_STOPPED;
        cover_stop_relays(runtime);
        (void)cover_store(runtime, false);
        cover_notify(runtime);
    } else if (relay_ctrl_is_on(open_relay)) {
        cover_start_applied(runtime, COVER_OPENING);
    } else if (relay_ctrl_is_on(close_relay)) {
        cover_start_applied(runtime, COVER_CLOSING);
    } else if (runtime->state.motion != COVER_STOPPED) {
        cover_finish(runtime, cover_stop_confidence(runtime));
    }
}

void cover_controller_init(void) {
    cover_count = 0;
}

uint8_t cover_controller_add(const cover_controller_config_t *config) {
    cover_runtime_t *    runtime;
    cover_state_record_t record;

    if (config == NULL || config->open_relay_id == config->close_relay_id ||
        config->open_relay_id >= relay_ctrl_count() ||
        config->close_relay_id >= relay_ctrl_count() || config->manual_max_run_ms == 0 ||
        config->default_closed_deadzone_bp >= COVER_POSITION_MAX_BP ||
        config->default_open_deadzone_bp >= COVER_POSITION_MAX_BP ||
        config->default_closed_deadzone_bp + config->default_open_deadzone_bp >=
        COVER_POSITION_MAX_BP || config->default_open_time_ms > COVER_MAX_TRAVEL_TIME_MS ||
        config->default_close_time_ms > COVER_MAX_TRAVEL_TIME_MS ||
        cover_count >= COVER_CONTROLLER_MAX_COVERS) return UINT8_MAX;

    runtime  = &cover_runtimes[cover_count];
    *runtime = (cover_runtime_t){
        .config = *config, .cover_id = cover_count,
        .state  = { .confidence         = COVER_POSITION_UNKNOWN,
                    .open_time_ms       = config->default_open_time_ms,
                    .close_time_ms      = config->default_close_time_ms,
                    .closed_deadzone_bp = config->default_closed_deadzone_bp,
                    .open_deadzone_bp   = config->default_open_deadzone_bp,
                    .overrun_ms         = config->default_overrun_ms },
    };
    cover_update_open_position(runtime);
    timer_init(&runtime->safety_timer, cover_safety_timeout, runtime);
    timer_init(&runtime->movement_timer, cover_movement_elapsed, runtime);
    timer_init(&runtime->position_timer, cover_position_elapsed, runtime);
    if (hal_nvm_read(config->nvm_item, sizeof(record),
                     (uint8_t *)&record) == HAL_NVM_SUCCESS &&
        record.version == COVER_STATE_VERSION &&
        record.closed_deadzone_bp < COVER_POSITION_MAX_BP &&
        record.open_deadzone_bp < COVER_POSITION_MAX_BP &&
        record.closed_deadzone_bp + record.open_deadzone_bp < COVER_POSITION_MAX_BP &&
        record.open_time_ms <= COVER_MAX_TRAVEL_TIME_MS &&
        record.close_time_ms <= COVER_MAX_TRAVEL_TIME_MS &&
        record.travel_position_bp <= COVER_POSITION_MAX_BP) {
        runtime->state.motor_reversal = record.motor_reversal != 0;
        runtime->state.confidence     = record.motion_active ? COVER_POSITION_UNKNOWN :
                                        record.confidence;
        runtime->state.open_time_ms       = record.open_time_ms;
        runtime->state.close_time_ms      = record.close_time_ms;
        runtime->state.closed_deadzone_bp = record.closed_deadzone_bp;
        runtime->state.open_deadzone_bp   = record.open_deadzone_bp;
        runtime->state.overrun_ms         = record.overrun_ms;
        runtime->state.travel_position_bp = record.travel_position_bp;
        cover_update_open_position(runtime);
    }
    if (!relay_ctrl_subscribe(config->open_relay_id, cover_relay_changed, runtime) ||
        !relay_ctrl_subscribe(config->close_relay_id, cover_relay_changed,
                              runtime)) return UINT8_MAX;

    cover_count++;
    return runtime->cover_id;
}

uint8_t cover_controller_count(void) {
    return cover_count;
}

bool cover_controller_subscribe(uint8_t cover_id, cover_state_cb_t callback, void *context) {
    cover_runtime_t *runtime;

    if (cover_id >= cover_count || callback == NULL) return false;

    runtime = &cover_runtimes[cover_id];
    for (uint8_t i = 0; i < COVER_CONTROLLER_MAX_OBSERVERS; i++) {
        if (runtime->observers[i].callback == callback && runtime->observers[i].context == context)
            return true;

        if (runtime->observers[i].callback == NULL) {
            runtime->observers[i].callback = callback;
            runtime->observers[i].context  = context;
            return true;
        }
    }
    return false;
}

cover_motion_t cover_controller_motion(uint8_t cover_id) {
    return cover_id < cover_count ? cover_runtimes[cover_id].state.motion : COVER_STOPPED;
}

bool cover_controller_state(uint8_t cover_id, cover_state_t *state) {
    if (cover_id >= cover_count || state == NULL) return false;

    *state = cover_runtimes[cover_id].state;
    return true;
}

uint8_t cover_controller_lift_percentage(uint8_t cover_id) {
    cover_state_t state;

    if (!cover_controller_state(cover_id, &state) || state.confidence == COVER_POSITION_UNKNOWN)
        return 0xff;

    return (uint8_t)(100 - ((state.open_position_bp + 50) / 100));
}

bool cover_controller_motor_reversal(uint8_t cover_id) {
    return cover_id < cover_count && cover_runtimes[cover_id].state.motor_reversal;
}

cover_result_t cover_stop(uint8_t cover_id) {
    cover_runtime_t *runtime;
    bool             was_active;

    if (cover_id >= cover_count) return COVER_RESULT_INVALID;

    runtime    = &cover_runtimes[cover_id];
    was_active = runtime->state.motion != COVER_STOPPED || runtime->prepared ||
                 timer_is_active(&runtime->safety_timer) ||
                 timer_is_active(&runtime->movement_timer);
    if (!was_active) return COVER_RESULT_UNCHANGED;

    cover_finish(runtime, cover_stop_confidence(runtime));
    return COVER_RESULT_APPLIED;
}

static cover_result_t cover_start(uint8_t cover_id, cover_motion_t direction,
                                  uint16_t target_travel_bp, bool endpoint_target) {
    cover_runtime_t *runtime;
    uint8_t          target_relay;
    relay_result_t   result;
    cover_state_t    before;

    if (cover_id >= cover_count) return COVER_RESULT_INVALID;

    runtime      = &cover_runtimes[cover_id];
    target_relay = direction == COVER_OPENING ? cover_open_relay(runtime) :
                   cover_close_relay(runtime);
    cover_update_position(runtime);
    if (runtime->state.motion == direction && relay_ctrl_is_on(target_relay)) {
        if (runtime->state.target_travel_bp == target_travel_bp) return COVER_RESULT_UNCHANGED;

        /* Same direction: retime the running motor, no stop and no NVM write. */
        runtime->endpoint_target        = endpoint_target;
        runtime->state.target_travel_bp = target_travel_bp;
        cover_start_applied(runtime, direction);
        return runtime->state.motion == direction ? COVER_RESULT_APPLIED :
               COVER_RESULT_BLOCKED;
    }

    cover_cancel_timers(runtime);
    runtime->prepared               = true;
    runtime->endpoint_target        = endpoint_target;
    runtime->state.target_travel_bp = target_travel_bp;
    before = runtime->state;
    if (!cover_store(runtime, true)) runtime->state.confidence = COVER_POSITION_UNKNOWN;
    cover_changed(runtime, before);
    before = runtime->state;
    runtime->state.motion = COVER_STOPPED;
    cover_changed(runtime, before);
    runtime->stopping = true;
    cover_stop_relays(runtime);
    runtime->stopping = false;
    result            = relay_ctrl_submit(&(relay_request_t){ .relay_id = target_relay,
                                                              .type     = RELAY_REQUEST_ON,
                                                              .source   = RELAY_SOURCE_COVER });
    if (result == RELAY_RESULT_BLOCKED || result == RELAY_RESULT_INVALID) {
        runtime->prepared = false;
        (void)cover_store(runtime, false);
    }

    /* An applied relay starts the motor synchronously; it is stopped again
     * when no cutoff timer could be armed. */
    if (result == RELAY_RESULT_APPLIED && runtime->state.motion != direction)
        return COVER_RESULT_BLOCKED;

    return cover_result_from_relay(result);
}

cover_result_t cover_open(uint8_t cover_id) {
    return cover_start(cover_id, COVER_OPENING, COVER_POSITION_MAX_BP, true);
}

cover_result_t cover_close(uint8_t cover_id) {
    return cover_start(cover_id, COVER_CLOSING, 0, true);
}

cover_result_t cover_goto(uint8_t cover_id, uint16_t open_position_bp) {
    cover_runtime_t *runtime;
    cover_motion_t   direction;
    uint16_t         target;
    uint16_t         distance;
    uint32_t         usable;

    if (cover_id >= cover_count || open_position_bp > COVER_POSITION_MAX_BP)
        return COVER_RESULT_INVALID;

    runtime = &cover_runtimes[cover_id];
    if (cover_is_manual(runtime)) return COVER_RESULT_BLOCKED;

    if (open_position_bp == 0) return cover_close(cover_id);

    if (open_position_bp == COVER_POSITION_MAX_BP) return cover_open(cover_id);

    /* An intermediate target needs a trustworthy start; OPEN or CLOSE
     * re-establishes it. */
    if (runtime->state.confidence == COVER_POSITION_UNKNOWN) return COVER_RESULT_BLOCKED;

    cover_update_position(runtime);
    usable = COVER_POSITION_MAX_BP - runtime->state.closed_deadzone_bp -
             runtime->state.open_deadzone_bp;
    target = (uint16_t)(runtime->state.closed_deadzone_bp +
                        ((uint32_t)open_position_bp * usable) / COVER_POSITION_MAX_BP);
    direction = target >= runtime->state.travel_position_bp ? COVER_OPENING : COVER_CLOSING;
    distance  = direction == COVER_OPENING ? target - runtime->state.travel_position_bp :
                runtime->state.travel_position_bp - target;
    /* Less than 1 ms of travel away: the cover is already there. */
    if ((uint32_t)distance * cover_effective_time(runtime, direction) < COVER_POSITION_MAX_BP)
        return cover_stop(cover_id);

    return cover_start(cover_id, direction, target, false);
}

cover_result_t cover_controller_set_motor_reversal(uint8_t cover_id, bool motor_reversal) {
    cover_runtime_t *runtime;
    cover_state_t    before;

    if (cover_id >= cover_count) return COVER_RESULT_INVALID;

    runtime = &cover_runtimes[cover_id];
    (void)cover_stop(cover_id);
    if (runtime->state.motor_reversal == motor_reversal) return COVER_RESULT_UNCHANGED;

    before = runtime->state;
    runtime->state.motor_reversal = motor_reversal;
    if (!cover_store(runtime, false)) {
        runtime->state.motor_reversal = before.motor_reversal;
        return COVER_RESULT_INVALID;
    }
    cover_changed(runtime, before);
    return COVER_RESULT_APPLIED;
}

bool cover_controller_settings_valid(const cover_state_t *state) {
    return state->closed_deadzone_bp < COVER_POSITION_MAX_BP &&
           state->open_deadzone_bp < COVER_POSITION_MAX_BP &&
           state->closed_deadzone_bp + state->open_deadzone_bp < COVER_POSITION_MAX_BP &&
           state->open_time_ms <= COVER_MAX_TRAVEL_TIME_MS &&
           state->close_time_ms <= COVER_MAX_TRAVEL_TIME_MS;
}

static cover_result_t cover_set_state(uint8_t cover_id, cover_state_t state) {
    cover_runtime_t *runtime;
    cover_state_t    before;

    if (cover_id >= cover_count || !cover_controller_settings_valid(&state))
        return COVER_RESULT_INVALID;

    runtime = &cover_runtimes[cover_id];
    if (runtime->state.open_time_ms == state.open_time_ms &&
        runtime->state.close_time_ms == state.close_time_ms &&
        runtime->state.closed_deadzone_bp == state.closed_deadzone_bp &&
        runtime->state.open_deadzone_bp == state.open_deadzone_bp &&
        runtime->state.overrun_ms == state.overrun_ms) return COVER_RESULT_UNCHANGED;

    (void)cover_stop(cover_id);
    before = runtime->state;
    runtime->state.open_time_ms       = state.open_time_ms;
    runtime->state.close_time_ms      = state.close_time_ms;
    runtime->state.closed_deadzone_bp = state.closed_deadzone_bp;
    runtime->state.open_deadzone_bp   = state.open_deadzone_bp;
    runtime->state.overrun_ms         = state.overrun_ms;
    cover_update_open_position(runtime);
    if (!cover_store(runtime, false)) {
        runtime->state = before;
        return COVER_RESULT_INVALID;
    }
    cover_changed(runtime, before);
    return COVER_RESULT_APPLIED;
}

cover_result_t cover_controller_set_open_time(uint8_t cover_id, uint32_t value) {
    cover_state_t state;

    if (!cover_controller_state(cover_id, &state)) return COVER_RESULT_INVALID;

    state.open_time_ms = value;
    return cover_set_state(cover_id, state);
}

cover_result_t cover_controller_set_close_time(uint8_t cover_id, uint32_t value) {
    cover_state_t state;

    if (!cover_controller_state(cover_id, &state)) return COVER_RESULT_INVALID;

    state.close_time_ms = value;
    return cover_set_state(cover_id, state);
}

cover_result_t cover_controller_set_closed_deadzone(uint8_t cover_id, uint16_t value) {
    cover_state_t state;

    if (!cover_controller_state(cover_id, &state)) return COVER_RESULT_INVALID;

    state.closed_deadzone_bp = value;
    return cover_set_state(cover_id, state);
}

cover_result_t cover_controller_set_open_deadzone(uint8_t cover_id, uint16_t value) {
    cover_state_t state;

    if (!cover_controller_state(cover_id, &state)) return COVER_RESULT_INVALID;

    state.open_deadzone_bp = value;
    return cover_set_state(cover_id, state);
}

cover_result_t cover_controller_set_overrun(uint8_t cover_id, uint16_t value) {
    cover_state_t state;

    if (!cover_controller_state(cover_id, &state)) return COVER_RESULT_INVALID;

    state.overrun_ms = value;
    return cover_set_state(cover_id, state);
}

void cover_controller_action(void *context, uint8_t cover_id, uint16_t action) {
    (void)context;
    if (action == COVER_ACTION_OPEN) (void)cover_open(cover_id);
    else if (action == COVER_ACTION_CLOSE) (void)cover_close(cover_id);
    else if (action == COVER_ACTION_STOP) (void)cover_stop(cover_id);
}
