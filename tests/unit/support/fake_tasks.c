#include "fake_tasks.h"
#include "hal/tasks.h"
#include "hal/timer.h"
#include <stdbool.h>
#include <stddef.h>

#define FAKE_TASKS_MAX    32

typedef struct {
    hal_task_t *task;
    uint32_t    deadline_ms;
    bool        active;
} fake_task_entry_t;

static fake_task_entry_t fake_task_entries[FAKE_TASKS_MAX];

static bool fake_tasks_due(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

void fake_tasks_reset(void) {
    for (uint8_t i = 0; i < FAKE_TASKS_MAX; i++) {
        fake_task_entries[i].task   = NULL;
        fake_task_entries[i].active = false;
    }
}

void fake_tasks_poll(void) {
    bool ran;

    do {
        ran = false;
        for (uint8_t i = 0; i < FAKE_TASKS_MAX; i++) {
            fake_task_entry_t *entry = &fake_task_entries[i];

            if (!entry->active ||
                !fake_tasks_due(hal_millis(), entry->deadline_ms)) {
                continue;
            }

            hal_task_t *task = entry->task;
            entry->active = false;
            if (task != NULL && task->handler != NULL) {
                task->handler(task->arg);
            }
            ran = true;
        }
    } while (ran);
}

uint8_t fake_tasks_pending(void) {
    uint8_t count = 0;

    for (uint8_t i = 0; i < FAKE_TASKS_MAX; i++) {
        if (fake_task_entries[i].active) {
            count++;
        }
    }
    return count;
}

void hal_tasks_init(hal_task_t *task) {
    (void)task;
}

void hal_tasks_schedule(hal_task_t *task, uint32_t delay_ms) {
    fake_task_entry_t *free_entry = NULL;

    for (uint8_t i = 0; i < FAKE_TASKS_MAX; i++) {
        if (fake_task_entries[i].active &&
            fake_task_entries[i].task == task) {
            fake_task_entries[i].deadline_ms = hal_millis() + delay_ms;
            return;
        }
        if (!fake_task_entries[i].active && free_entry == NULL) {
            free_entry = &fake_task_entries[i];
        }
    }

    if (free_entry != NULL) {
        free_entry->task        = task;
        free_entry->deadline_ms = hal_millis() + delay_ms;
        free_entry->active      = true;
    }
}

void hal_tasks_unschedule(hal_task_t *task) {
    for (uint8_t i = 0; i < FAKE_TASKS_MAX; i++) {
        if (fake_task_entries[i].active &&
            fake_task_entries[i].task == task) {
            fake_task_entries[i].active = false;
        }
    }
}
