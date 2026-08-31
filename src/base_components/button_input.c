#include "button_input.h"

#include "base_components/button_dispatcher.h"
#include "base_components/gpio_edge_queue.h"
#include "hal/tasks.h"
#include "hal/timer.h"
#include <stddef.h>

static button_config_t   button_configs[BUTTON_INPUT_MAX];
static button_runtime_t  button_runtimes[BUTTON_INPUT_MAX];
static uint8_t           button_count;
static uint32_t          button_event_seq;
static uint32_t          button_events_emitted;
static gpio_edge_queue_t button_edge_queue;
static hal_task_t        button_worker_task;
static volatile bool     button_worker_scheduled;
static volatile bool     button_work_pending;

static button_state_t button_input_level_to_state(uint8_t button_id,
                                                  uint8_t level) {
    return (level != 0) == (button_configs[button_id].active_high != 0)
           ? BUTTON_STATE_DOWN
           : BUTTON_STATE_UP;
}

static void button_input_commit(uint8_t button_id, button_state_t state,
                                uint32_t timestamp_ms) {
    button_runtime_t *runtime = &button_runtimes[button_id];
    button_event_t    event;

    runtime->stable_state = state;
    if (state == BUTTON_STATE_DOWN) {
        runtime->press_id++;
    }
    event.button_id = button_id;
    event.type      = state == BUTTON_STATE_DOWN ? BUTTON_EVENT_DOWN
                                                    : BUTTON_EVENT_UP;
    event.timestamp_ms = timestamp_ms;
    event.seq          = button_event_seq++;
    event.press_id     = runtime->press_id;
    button_events_emitted++;
    button_dispatcher_emit(&event);
}

static void button_input_process_edge(const hal_gpio_edge_t *edge) {
    for (uint8_t button_id = 0; button_id < button_count; button_id++) {
        button_runtime_t *runtime;
        button_state_t    new_state;

        if (button_configs[button_id].pin != edge->pin) {
            continue;
        }
        runtime   = &button_runtimes[button_id];
        new_state = button_input_level_to_state(button_id, edge->level);
        if (new_state == runtime->raw_state) {
            return;
        }
        if (runtime->raw_state != runtime->stable_state &&
            edge->timestamp_ms - runtime->raw_since_ms >=
            button_configs[button_id].debounce_ms) {
            button_input_commit(button_id, runtime->raw_state,
                                runtime->raw_since_ms);
        }
        runtime->raw_state    = new_state;
        runtime->raw_since_ms = edge->timestamp_ms;
        return;
    }
}

static void button_input_schedule_worker(uint32_t delay_ms) {
    if (button_worker_scheduled) {
        hal_tasks_unschedule(&button_worker_task);
    }
    button_worker_scheduled = true;
    hal_tasks_schedule(&button_worker_task, delay_ms);
}

static void button_input_worker(void *arg) {
    hal_gpio_edge_t edge;
    uint32_t        now            = hal_millis();
    uint32_t        earliest_delay = UINT32_MAX;

    (void)arg;
    button_worker_scheduled = false;
    while (gpio_edge_queue_pop(&button_edge_queue, &edge)) {
        button_input_process_edge(&edge);
    }
    for (uint8_t button_id = 0; button_id < button_count; button_id++) {
        button_runtime_t *runtime     = &button_runtimes[button_id];
        uint16_t          debounce_ms = button_configs[button_id].debounce_ms;
        uint32_t          elapsed;

        if (runtime->raw_state == runtime->stable_state) {
            continue;
        }
        elapsed = now - runtime->raw_since_ms;
        if (elapsed >= debounce_ms) {
            button_input_commit(button_id, runtime->raw_state,
                                runtime->raw_since_ms);
        } else if ((uint32_t)(debounce_ms - elapsed) < earliest_delay) {
            earliest_delay = debounce_ms - elapsed;
        }
    }
    if (gpio_edge_queue_used(&button_edge_queue) != 0) {
        button_input_schedule_worker(0);
    } else if (earliest_delay != UINT32_MAX) {
        button_input_schedule_worker(earliest_delay);
    }
}

static void button_input_edge_sink(const hal_gpio_edge_t *edge) {
    gpio_edge_queue_push(&button_edge_queue, edge);
    button_work_pending = true;
}

void button_input_init(void) {
    button_count            = 0;
    button_event_seq        = 0;
    button_events_emitted   = 0;
    button_worker_scheduled = false;
    button_work_pending     = false;
    gpio_edge_queue_init(&button_edge_queue);
    button_dispatcher_init();
    button_worker_task.handler = button_input_worker;
    button_worker_task.arg     = NULL;
    hal_tasks_init(&button_worker_task);
    hal_gpio_set_edge_sink(button_input_edge_sink);
}

void button_input_process_pending(void) {
    if (!button_work_pending) {
        return;
    }

    button_work_pending = false;
    if (!button_worker_scheduled) {
        button_input_schedule_worker(0);
    }
}

uint8_t button_input_add(const button_config_t *config) {
    uint8_t button_id = button_count;
    uint8_t level;

    if (config == NULL || button_count >= BUTTON_INPUT_MAX) {
        return UINT8_MAX;
    }
    button_configs[button_id] = *config;
    if (button_configs[button_id].debounce_ms == 0) {
        button_configs[button_id].debounce_ms = BUTTON_DEBOUNCE_DEFAULT_MS;
    }
    level = hal_gpio_read(config->pin);
    button_runtimes[button_id].stable_state =
        button_input_level_to_state(button_id, level);
    button_runtimes[button_id].raw_state =
        button_runtimes[button_id].stable_state;
    button_runtimes[button_id].raw_since_ms = hal_millis();
    button_runtimes[button_id].press_id     = 0;
    button_runtimes[button_id].boot_press   =
        button_runtimes[button_id].stable_state == BUTTON_STATE_DOWN;
    button_count++;
    hal_gpio_watch_pin(config->pin);
    return button_id;
}

void button_input_set_active_high(uint8_t button_id, bool active_high) {
    uint8_t level;

    if (button_id >= button_count) {
        return;
    }
    button_configs[button_id].active_high = active_high;
    level = hal_gpio_read(button_configs[button_id].pin);
    button_runtimes[button_id].stable_state =
        button_input_level_to_state(button_id, level);
    button_runtimes[button_id].raw_state =
        button_runtimes[button_id].stable_state;
    button_runtimes[button_id].raw_since_ms = hal_millis();
}

void button_input_apply_switch_mode(uint8_t button_id, bool momentary_nc) {
    if (button_id >= button_count) {
        return;
    }
    button_input_set_active_high(
        button_id, button_configs[button_id].electrical_active_high !=
        momentary_nc);
}

void button_input_set_debounce_ms(uint8_t button_id, uint16_t debounce_ms) {
    if (button_id < button_count && debounce_ms >= 1 && debounce_ms <= 200) {
        button_configs[button_id].debounce_ms = debounce_ms;
    }
}

bool button_input_is_down(uint8_t button_id) {
    return button_id < button_count &&
           button_runtimes[button_id].stable_state == BUTTON_STATE_DOWN;
}

bool button_input_boot_press(uint8_t button_id) {
    return button_id < button_count && button_runtimes[button_id].boot_press;
}

uint8_t button_input_count(void) {
    return button_count;
}

uint32_t button_input_gpio_edges_dropped(void) {
    return button_edge_queue.dropped;
}

uint32_t button_input_events_emitted(void) {
    return button_events_emitted;
}
