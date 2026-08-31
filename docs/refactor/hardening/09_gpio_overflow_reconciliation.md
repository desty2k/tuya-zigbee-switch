# 09 — GPIO overflow reconciliation

Phase 2, priority 3.

## Problem

The edge ring correctly keeps older history and drops a new edge on overflow.
Once any transition is lost, however, the button FSM may finish in a logical
state that differs from the pin. A dropped release can leave a button down and a
hold active until another physical transition.

## Loss epoch

`gpio_edge_queue_push()` already increments `dropped`. It additionally marks an
overflow epoch through a sticky flag or generation counter. The consumer takes a
snapshot of that marker when it begins a drain.

Reconciliation runs only after all preserved entries from the affected epoch
have been processed. Resampling before the queue drains would reorder hardware
truth ahead of older captured history.

## Reconciliation procedure

```text
drain every preserved edge in FIFO order
if overflow epoch is pending:
    now = hal_millis()
    for each configured button:
        sampled = logical state derived from hal_gpio_read(pin)
        reconcile(button, sampled, now)
    mark epoch reconciled
```

For an FSM already equal to the sample, no action occurs. For a mismatch:

- cancel pending debounce and gesture deadlines for that button;
- set raw and stable state to the sample;
- reset `raw_since_ms` to the reconciliation time;
- if the sample is down, allocate a recovery press identity and mark it
  `reconciled_press`;
- emit at most the minimum state transition needed to release downstream state;
- never synthesize `N_CLICK`, hold or binding commands from unknown history.

The release case must clear a stuck logical press in consumers. It emits a
reconciled `UP` carrying the current press id, and gesture consumers treat the
reconciliation flag as cancellation rather than a completed click. The down case
updates current state but does not actuate a relay or binding; ordinary behavior
resumes after a real release and next press.

This requires a provenance flag on internal button events or a dedicated
dispatcher cancellation method. It does not change the external `0xFC02` payload;
recovery transitions are state repair, not user actions.

## Multiple overflows

A generation counter avoids losing an overflow that occurs during
reconciliation. The consumer records the generation it reconciled; if the
producer advances it again, another reconciliation runs after the next drain.
Counter wrap is handled by inequality, not ordering.

## Diagnostics

| Counter | Meaning |
| --- | --- |
| `gpio_edges_dropped` | individual failed pushes |
| `gpio_overflow_epochs` | distinct loss epochs observed by producer |
| `gpio_reconciliations` | completed full-input resamples |
| `gpio_state_repairs` | buttons whose logical state differed from hardware |

## Acceptance

- Force a queue overflow whose dropped edge is `UP`; after drain, button state is
  released and no hold/click remains active.
- Force a dropped `DOWN`; state converges without toggling a relay or sending a
  binding action.
- Overflow two buttons and reconcile both from one snapshot.
- Inject another overflow during reconciliation and prove a second epoch runs.
- Ordinary no-overflow paths emit byte-for-byte identical events and do no extra
  GPIO reads in the worker.
- Every hardware stress run keeps overflow counters at zero; reconciliation is a
  safety net, not an accepted steady-state behavior.

