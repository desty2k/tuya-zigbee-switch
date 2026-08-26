#include "timer_service.h"

#include "hal/timer.h"

static void timer_task_handler(void *arg) {
    app_timer_t *timer = (app_timer_t *)arg;

    timer->active = false;
    timer->callback(timer->arg);
}

void timer_init(app_timer_t *timer, timer_callback_t callback, void *arg) {
    timer->task.handler = timer_task_handler;
    timer->task.arg     = timer;
    timer->callback     = callback;
    timer->arg          = arg;
    timer->deadline_ms  = 0;
    timer->active       = false;
    hal_tasks_init(&timer->task);
}

void timer_start(app_timer_t *timer, uint32_t delay_ms) {
    if (timer->active) {
        return;
    }

    timer_restart(timer, delay_ms);
}

void timer_restart(app_timer_t *timer, uint32_t delay_ms) {
    if (timer->active) {
        hal_tasks_unschedule(&timer->task);
    }
    timer->deadline_ms = hal_millis() + delay_ms;
    timer->active      = true;
    hal_tasks_schedule(&timer->task, delay_ms);
}

void timer_cancel(app_timer_t *timer) {
    if (!timer->active) {
        return;
    }

    hal_tasks_unschedule(&timer->task);
    timer->active = false;
}

bool timer_is_active(const app_timer_t *timer) {
    return timer->active;
}

uint32_t timer_remaining_ms(const app_timer_t *timer) {
    uint32_t remaining;

    if (!timer->active) {
        return 0;
    }

    remaining = timer->deadline_ms - hal_millis();
    return (int32_t)remaining > 0 ? remaining : 0;
}
