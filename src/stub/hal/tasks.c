#include "hal/tasks.h"
#include "hal/timer.h"
#include "stub/hal/stub.h"
#include "stub/machine_io.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_TASKS    32

/*
 * Deadlines use the 64-bit virtual clock rather than hal_millis(): the
 * platform schedulers count down their own timers and are not affected by a
 * wrap of the millisecond value the firmware sees.
 */
typedef struct {
    hal_task_t *task;
    uint64_t    scheduled_time;
    int         active;
} stub_task_entry_t;

static stub_task_entry_t tasks[MAX_TASKS];
static int      tasks_paused;
static uint32_t tasks_rejections;

void stub_tasks_reject_next(uint32_t count) {
    tasks_rejections = count;
}

void stub_tasks_poll(void) {
    uint64_t current_time;
    int      tasks_executed = 0;

    if (tasks_paused) {
        return;
    }
    current_time = stub_millis_virtual();

    // Check all tasks for execution, keep looping until no tasks are executed in
    // a pass, to more agressively tests for tasks that reschedule themselves.
    do {
        tasks_executed = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].active && current_time >= tasks[i].scheduled_time) {
                hal_task_t *task = tasks[i].task;

                // Free the slot first: the handler may reschedule the task.
                tasks[i].active = 0;
                if (task && task->handler) {
                    io_log("TASKS", "Executing task %p from slot %d",
                           (void *)task, i);
                    task->handler(task->arg);
                    tasks_executed++;
                }
                io_log("TASKS", "Task completed and removed from slot %d", i);
            }
        }
    } while (tasks_executed > 0);
}

bool stub_tasks_next_due(uint64_t *due) {
    bool found = false;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].active &&
            (!found || tasks[i].scheduled_time < *due)) {
            *due  = tasks[i].scheduled_time;
            found = true;
        }
    }
    return found;
}

void stub_tasks_pause(void) {
    tasks_paused = 1;
}

void stub_tasks_resume(void) {
    tasks_paused = 0;
}

uint32_t stub_tasks_active_count(void) {
    uint32_t count = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].active) {
            count++;
        }
    }
    return count;
}

void hal_tasks_init(hal_task_t *task) {
    if (!task) {
        io_log("TASKS", "Error: NULL task pointer passed to hal_init_task");
        exit(1);
    }

    memset(&task->platform_struct, 0, sizeof(task->platform_struct));
    io_log("TASKS", "Initialized task at %p", (void *)task);
}

bool hal_tasks_schedule(hal_task_t *task, uint32_t delay_ms) {
    if (!task) {
        io_log("TASKS", "Error: NULL task pointer passed to hal_schedule_task");
        exit(1);
    }

    if (!task->handler) {
        io_log("TASKS", "Error: Task at %p has NULL handler", (void *)task);
        exit(1);
    }

    // Like the Telink event timer, a rejected schedule also drops the old one.
    hal_tasks_unschedule(task);
    if (tasks_rejections > 0) {
        tasks_rejections--;
        io_log("TASKS", "Rejected schedule of task %p", (void *)task);
        return false;
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        io_log("TASKS", "Error: No free task slots available (max %d tasks)",
               MAX_TASKS);
        exit(1);
    }

    tasks[slot].task           = task;
    tasks[slot].scheduled_time = stub_millis_virtual() + delay_ms;
    tasks[slot].active         = 1;

    io_log("TASKS", "Scheduled task %p in slot %d, delay=%u ms, execute_at=%llu",
           (void *)task, slot, delay_ms,
           (unsigned long long)tasks[slot].scheduled_time);
    return true;
}

void hal_tasks_unschedule(hal_task_t *task) {
    if (!task)
        return;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].active && tasks[i].task == task) {
            tasks[i].active = 0;
            io_log("TASKS", "Unscheduled task %p from slot %d", (void *)task, i);
            break;
        }
    }
}
