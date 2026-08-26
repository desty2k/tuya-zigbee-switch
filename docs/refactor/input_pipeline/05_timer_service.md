# 05 — Timer service

Single generic deadline abstraction for every feature that needs "run X in Y ms".
It knows a deadline and a callback, never what the deadline means.

## Types and API

```c
typedef void (*timer_callback_t)(void *arg);

typedef struct {
    hal_task_t       task;         // platform scheduling primitive
    timer_callback_t callback;
    void *           arg;
    uint32_t         deadline_ms;
    bool             active;
} app_timer_t;

void     timer_init(app_timer_t *t, timer_callback_t cb, void *arg);
void     timer_start(app_timer_t *t, uint32_t delay_ms);    // no-op if already active
void     timer_restart(app_timer_t *t, uint32_t delay_ms);  // always re-arms
void     timer_cancel(app_timer_t *t);
bool     timer_is_active(const app_timer_t *t);
uint32_t timer_remaining_ms(const app_timer_t *t);          // 0 when inactive
```

## Semantics

- One-shot. `active` is cleared before the callback runs, so a callback may
  restart its own timer.
- `timer_start` on an active timer keeps the existing deadline;
  `timer_restart` replaces it. Callers pick the one matching their policy.
- `delay_ms == 0` runs the callback from the next task dispatch, never inline.
- `timer_cancel` on an inactive timer is a no-op.
- Deadlines are absolute (`hal_millis() + delay_ms`) and wraparound-safe through
  unsigned comparison.
- Callbacks run in task context with the same guarantees as `hal_tasks`.
- One `app_timer_t` maps to one `hal_task_t`; there is no central timer list, no
  shared dispatch ordering and no priority inversion between features.

## Ownership

| Owner | Timer | Purpose |
| --- | --- | --- |
| `gesture_fsm` | `hold_timer` | hold threshold |
| `gesture_fsm` | `multi_click_timer` | click sequence gap |
| `relay_controller` | `auto_off_timer` | inching pulse and on-with-timed-off |
| `relay_driver` | `latch_timer` | latching relay coil pulse |
| `interlock` | `dead_time_timer` (per group) | delayed ON after switching peers off |
| `cover_cluster` | `movement_timer` | motor protection / reversal sequencing |
| `led` | `blink_timer` | indicator blinking |
| `system_action` | `action_timer` | delayed reset / reboot |

`button_input` keeps its own worker `hal_task_t` rather than an `app_timer_t`,
because it is a work-pump rather than a feature deadline.
