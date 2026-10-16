#include "button_dispatcher.h"

#include <stddef.h>

typedef struct {
    button_event_sink_t sink;
    void *              arg;
} button_sink_entry_t;

static button_sink_entry_t button_sinks[BUTTON_SINK_MAX];
static uint8_t             button_sink_count;
static button_lock_t       button_lock;

void button_dispatcher_init(void) {
    button_sink_count = 0;
    button_lock       = NULL;
}

void button_dispatcher_set_lock(button_lock_t lock) {
    button_lock = lock;
}

void button_dispatcher_register(button_event_sink_t sink, void *arg) {
    if (sink == NULL || button_sink_count >= BUTTON_SINK_MAX) {
        return;
    }
    button_sinks[button_sink_count].sink = sink;
    button_sinks[button_sink_count].arg  = arg;
    button_sink_count++;
}

void button_dispatcher_emit(const button_event_t *event) {
    button_event_t stamped = *event;

    stamped.locked = button_lock != NULL && button_lock(event);
    for (uint8_t i = 0; i < button_sink_count; i++) {
        button_sinks[i].sink(&stamped, button_sinks[i].arg);
    }
}
