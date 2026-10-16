#ifndef _BUTTON_DISPATCHER_H_
#define _BUTTON_DISPATCHER_H_

#include "base_components/button_input.h"

/* Consumers: switch cluster, cover switch cluster, button event cluster,
 * gesture FSM, child lock, and one platform observer. Registration beyond
 * the bound is ignored, so this must be raised whenever a consumer is added. */
#define BUTTON_SINK_MAX    6

typedef void (*button_event_sink_t)(const button_event_t *event, void *arg);

/* Decides whether an event's press is locked; called once per event, before
 * any sink, so every sink sees the same `locked`. */
typedef bool (*button_lock_t)(const button_event_t *event);

void button_dispatcher_init(void);
void button_dispatcher_register(button_event_sink_t sink, void *arg);
void button_dispatcher_set_lock(button_lock_t lock);
void button_dispatcher_emit(const button_event_t *event);

#endif
