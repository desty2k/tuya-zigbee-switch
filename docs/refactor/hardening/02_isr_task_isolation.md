# 02 — ISR to task scheduler isolation

Phase 1, priority 1.

## Risk

The Telink GPIO interrupt path reaches generic timer scheduling in two ways:

1. `gpio_isr_callback()` calls `hal_tasks_schedule(&gpio_dispatch_task, ...)`.
2. ISR-side `capture_snapshot()` calls the registered edge sink;
   `button_input_edge_sink()` calls `hal_tasks_schedule(&button_worker_task, 0)`.

The Telink `hal_tasks_schedule()` implementation may call both
`ev_timer_taskCancel()` and `ev_timer_taskPost()`. The hardening contract does not
depend on those SDK functions being interrupt-safe.

## Target contract

```c
/** Service GPIO work requested by an IRQ. Called from normal context. */
void hal_gpio_process_pending(void);

/** Schedule or run application input work requested by the edge sink. */
void button_input_process_pending(void);
```

The application poll calls them in this order:

```text
hal_gpio_process_pending()
button_input_process_pending()
```

This ordering lets Telink finish its masked-IRQ snapshot/re-arm sequence before
the button layer consumes the captured batch.

## Telink IRQ procedure

```text
on GPIO IRQ:
    increment gpio_irq_count
    capture one all-port snapshot
    for each changed watched pin:
        update last_level
        push immutable edge through the IRQ-safe sink
    disable watched GPIO IRQs
    gpio_dispatch_pending = true
    return
```

The edge sink performs exactly:

```text
push edge into gpio_edge_queue
button_work_pending = true
return
```

It does not inspect task handles and does not post or cancel a task.

## Normal-context procedure

`hal_gpio_process_pending()` atomically claims `gpio_dispatch_pending`. If no work
is pending it returns. Otherwise it runs the existing bounded polarity/re-arm
loop, enables watched IRQs and performs the final capture. Captures made here use
the same sink but are already in normal context.

`button_input_process_pending()` atomically claims `button_work_pending` and
schedules the worker at delay zero if it is not already scheduled. Debounce
deadline replacement and cancellation remain inside normal-context worker/task
code.

The pending flags use a small platform critical section or an atomic exchange;
plain read-clear is insufficient because an IRQ can set a flag between those
operations. The critical section contains no GPIO reads, queue drains or task
calls.

## Race invariants

1. Setting a pending flag twice before a poll is harmless; the ring stores the
   individual edges.
2. A flag set while normal context claims the previous flag causes a later poll.
3. GPIO IRQs remain masked from the first interrupt until the normal-context
   re-arm procedure completes.
4. Queue producer/consumer ownership stays SPSC: capture context writes `head`;
   button worker writes `tail`.
5. A debounce task already armed for a future deadline is replaced only by
   normal-context code when new queued history requires earlier processing.
6. The watchdog is serviced because both pending processors do bounded work.

## Platform behavior

| Platform | `hal_gpio_process_pending()` |
| --- | --- |
| Telink | Claims the IRQ work flag and performs the bounded re-arm loop |
| Silabs | No-op unless its HAL later adopts deferred capture |
| Stub | Deterministic no-op or execution of an explicitly injected deferred batch |

`button_input_process_pending()` is shared and required on every platform so
tests exercise the same scheduling boundary.

## Diagnostics

Add `gpio_work_wakeups` and `gpio_work_coalesced` only to debug/stub output if
needed during development. Release acceptance depends on the existing capture,
drop and re-arm counters, not on a specific number of main-loop polls.

## Acceptance

- A static call-graph audit from `gpio_isr_callback` reaches no
  `hal_tasks_*`, `ev_timer_*`, Zigbee, relay or NVM symbol.
- Native tests inject an IRQ between pending-flag claim and clear and prove work
  is not stranded.
- Delayed worker tests preserve the exact edge order already specified by the
  input-pipeline design.
- On BSEED hardware, simultaneous and rapid presses do not increase
  `gpio_edges_dropped` or `gpio_rearm_limit_hits`.

