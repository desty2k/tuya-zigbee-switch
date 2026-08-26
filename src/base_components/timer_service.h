#ifndef _TIMER_SERVICE_H_
#define _TIMER_SERVICE_H_

#include "hal/tasks.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*timer_callback_t)(void *arg);

typedef struct {
    hal_task_t       task;
    timer_callback_t callback;
    void *           arg;
    uint32_t         deadline_ms;
    bool             active;
} app_timer_t;

void timer_init(app_timer_t *timer, timer_callback_t callback, void *arg);
void timer_start(app_timer_t *timer, uint32_t delay_ms);
void timer_restart(app_timer_t *timer, uint32_t delay_ms);
void timer_cancel(app_timer_t *timer);
bool timer_is_active(const app_timer_t *timer);
uint32_t timer_remaining_ms(const app_timer_t *timer);

#endif
