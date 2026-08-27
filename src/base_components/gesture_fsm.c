#include "gesture_fsm.h"

#include "base_components/button_dispatcher.h"
#include "hal/timer.h"
#include <stddef.h>

typedef struct {
    uint8_t     button_id;
    uint8_t     click_count;
    bool        configured;
    bool        button_down;
    bool        hold_active;
    bool        suppressed;
    uint32_t    active_press_id;
    uint32_t    click_press_id;
    uint32_t    down_at_ms;
    app_timer_t hold_timer;
    app_timer_t multi_click_timer;
} gesture_runtime_t;

typedef struct {
    gesture_sink_t sink;
    void *         arg;
} gesture_sink_entry_t;

static gesture_config_t     gesture_configs[BUTTON_INPUT_MAX];
static gesture_runtime_t    gesture_runtimes[BUTTON_INPUT_MAX];
static gesture_sink_entry_t gesture_sinks[GESTURE_SINK_MAX];
static uint8_t  gesture_sink_count;
static uint32_t gesture_events_emitted;

static void gesture_fsm_emit(uint8_t button_id, gesture_type_t type,
                             uint8_t count, uint16_t duration_ms,
                             uint32_t press_id, uint32_t timestamp_ms) {
    gesture_event_t event = {
        .button_id    = button_id,
        .type         = type,
        .count        = count,
        .duration_ms  = duration_ms,
        .press_id     = press_id,
        .timestamp_ms = timestamp_ms,
    };

    gesture_events_emitted++;
    for (uint8_t i = 0; i < gesture_sink_count; i++) {
        gesture_sinks[i].sink(&event, gesture_sinks[i].arg);
    }
}

static void gesture_fsm_hold_expired(void *arg) {
    gesture_runtime_t *runtime = (gesture_runtime_t *)arg;

    if (!runtime->button_down || runtime->suppressed) {
        return;
    }
    if (runtime->click_count != 0) {
        gesture_fsm_emit(runtime->button_id, GESTURE_N_CLICK,
                         runtime->click_count, 0, runtime->click_press_id,
                         hal_millis());
        runtime->click_count = 0;
    }
    runtime->hold_active = true;
    gesture_fsm_emit(runtime->button_id, GESTURE_HOLD_START, 0, 0,
                     runtime->active_press_id, hal_millis());
}

static void gesture_fsm_click_gap_expired(void *arg) {
    gesture_runtime_t *runtime = (gesture_runtime_t *)arg;

    if (runtime->click_count == 0) {
        return;
    }
    gesture_fsm_emit(runtime->button_id, GESTURE_N_CLICK,
                     runtime->click_count, 0, runtime->click_press_id,
                     hal_millis());
    runtime->click_count = 0;
}

static void gesture_fsm_button_event(const button_event_t *event, void *arg) {
    gesture_runtime_t *runtime;
    gesture_config_t * config;

    (void)arg;
    if (event->button_id >= BUTTON_INPUT_MAX ||
        !gesture_runtimes[event->button_id].configured) {
        return;
    }
    runtime = &gesture_runtimes[event->button_id];
    config  = &gesture_configs[event->button_id];

    if (event->type == BUTTON_EVENT_DOWN) {
        uint32_t elapsed;

        runtime->button_down     = true;
        runtime->active_press_id = event->press_id;
        runtime->down_at_ms      = event->timestamp_ms;
        runtime->hold_active     = false;
        timer_cancel(&runtime->multi_click_timer);
        if (config->hold_ms != 0 && !runtime->suppressed) {
            elapsed = hal_millis() - event->timestamp_ms;
            timer_restart(&runtime->hold_timer,
                          elapsed < config->hold_ms
                          ? config->hold_ms - elapsed
                          : 0);
        }
        return;
    }

    runtime->button_down = false;
    timer_cancel(&runtime->hold_timer);
    if (runtime->suppressed) {
        runtime->suppressed = false;
        return;
    }
    if (runtime->hold_active) {
        runtime->hold_active = false;
        gesture_fsm_emit(event->button_id, GESTURE_HOLD_END, 0,
                         (uint16_t)(event->timestamp_ms - runtime->down_at_ms),
                         event->press_id, event->timestamp_ms);
        return;
    }
    runtime->click_count++;
    runtime->click_press_id = event->press_id;
    if (runtime->click_count >= GESTURE_MAX_N_CLICK) {
        gesture_fsm_emit(event->button_id, GESTURE_N_CLICK,
                         runtime->click_count, 0, event->press_id,
                         event->timestamp_ms);
        runtime->click_count = 0;
        return;
    }
    timer_restart(&runtime->multi_click_timer, config->multi_click_gap_ms);
}

void gesture_fsm_init(void) {
    gesture_sink_count     = 0;
    gesture_events_emitted = 0;
    for (uint8_t i = 0; i < BUTTON_INPUT_MAX; i++) {
        gesture_runtimes[i].configured = false;
    }
    button_dispatcher_register(gesture_fsm_button_event, NULL);
}

void gesture_fsm_add(uint8_t button_id, const gesture_config_t *config) {
    gesture_runtime_t *runtime;

    if (button_id >= BUTTON_INPUT_MAX || config == NULL) {
        return;
    }
    gesture_configs[button_id] = *config;
    runtime                  = &gesture_runtimes[button_id];
    runtime->button_id       = button_id;
    runtime->click_count     = 0;
    runtime->configured      = true;
    runtime->button_down     = button_input_is_down(button_id);
    runtime->hold_active     = false;
    runtime->suppressed      = button_input_boot_press(button_id);
    runtime->active_press_id = 0;
    runtime->click_press_id  = 0;
    runtime->down_at_ms      = hal_millis();
    timer_init(&runtime->hold_timer, gesture_fsm_hold_expired, runtime);
    timer_init(&runtime->multi_click_timer, gesture_fsm_click_gap_expired,
               runtime);
}

void gesture_fsm_set_hold_ms(uint8_t button_id, uint16_t hold_ms) {
    if (button_id < BUTTON_INPUT_MAX &&
        gesture_runtimes[button_id].configured) {
        gesture_configs[button_id].hold_ms = hold_ms;
    }
}

void gesture_fsm_set_multi_click_gap_ms(uint8_t button_id, uint16_t gap_ms) {
    if (button_id < BUTTON_INPUT_MAX &&
        gesture_runtimes[button_id].configured && gap_ms >= 100 &&
        gap_ms <= 2000) {
        gesture_configs[button_id].multi_click_gap_ms = gap_ms;
    }
}

bool gesture_fsm_hold_active(uint8_t button_id) {
    return button_id < BUTTON_INPUT_MAX &&
           gesture_runtimes[button_id].configured &&
           gesture_runtimes[button_id].hold_active;
}

void gesture_fsm_register_sink(gesture_sink_t sink, void *arg) {
    if (sink == NULL || gesture_sink_count >= GESTURE_SINK_MAX) {
        return;
    }
    gesture_sinks[gesture_sink_count].sink = sink;
    gesture_sinks[gesture_sink_count].arg  = arg;
    gesture_sink_count++;
}

uint32_t gesture_fsm_events_emitted(void) {
    return gesture_events_emitted;
}
