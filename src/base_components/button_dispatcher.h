#ifndef _BUTTON_DISPATCHER_H_
#define _BUTTON_DISPATCHER_H_

#include "base_components/button_input.h"

#define BUTTON_SINK_MAX    6

typedef void (*button_event_sink_t)(const button_event_t *event, void *arg);

void button_dispatcher_init(void);
void button_dispatcher_register(button_event_sink_t sink, void *arg);
void button_dispatcher_emit(const button_event_t *event);

#endif
