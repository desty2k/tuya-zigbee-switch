# Decisions and deviations

Deviations from the original refactor plan, and why. Everything not listed here
follows the plan.

## Accepted as-is

State/event separation, no NVM for input runtime state, HAL limited to edges,
bounded edge queue with explicit drops, thin button layer, timestamp-based
debounce, `press_id`, synchronous dispatcher, immediate action layer, gesture
layer, generic feature-agnostic timer service, generic N-click, hold suppressing
clicks, action mapper, factory reset as a system action, relay controller as the
single relay owner, inching and timed-off inside the controller, interlock as a
controller policy on logical ids, immutable Zigbee button events, TX queue,
event sequence numbers, `send_report_attr` snapshot fix, config parser without
feature wiring, config/runtime split, phased migration.

## Changed

1. **Edge queue lives in the application, not in the HAL.** The HAL exposes a
   single IRQ-safe edge sink; `button_input` owns the queue. This keeps the queue
   unit-testable without any platform code and keeps three HAL implementations
   free of buffering policy.

2. **Debounce commit rule is specified exactly.** A raw segment is committed when
   a *later edge* proves it lasted `debounce_ms`, not only when the worker
   happens to run. Without this rule the "worker delayed" case still collapses
   `DOWN, UP` into nothing. The algorithm and its four critical cases are in
   [03_button_input.md](./03_button_input.md).

3. **`GESTURE_MAX_N_CLICK` emits immediately.** When the cap is reached the count
   cannot grow, so the event is emitted at that release instead of after the gap.
   With the default reset threshold of 10 (equal to the cap) the 10-click factory
   reset keeps its current instant feel.

4. **Multi-click gap default is 350 ms, not 275 ms.** 275 ms is uncomfortably
   fast for a 10-click sequence on wall switches; 800 ms (today's value) makes
   `N_CLICK(1)` latency too high for remotes. 350 ms is the compromise, and it is
   configurable per endpoint.

5. **Debounce default is 8 ms** (plan suggested 5–10 ms). Final value is
   confirmed by the hardware regression pass in stage 6, not by unit tests alone.

6. **A hold flushes the pending click sequence.** `click, click, hold` emits
   `N_CLICK(2)` before `HOLD_START` instead of discarding the clicks, so mixed
   gestures stay observable.

7. **Latching-relay coil pulses are separated from inching.** The existing
   bi-stable relay support (100 ms coil pulse, 50 ms retry, `SLP` flag) is a
   hardware concern and moves to `relay_driver`. `RELAY_REQUEST_PULSE` is the
   user-facing inching feature in `relay_controller`. The plan did not mention
   latching relays at all; conflating the two would break every latching device.

8. **Covers are integrated instead of exempted.** `cover_cluster` currently calls
   `relay_on/off` directly. It becomes a controller client, and its relay pair is
   registered as an implicit interlock group with a 200 ms dead time, which turns
   the existing "never energise both directions" rule into a structural
   guarantee. Cover movement sequencing and the `moving` attribute stay in the
   cover cluster because they are cover-domain policy.

9. **Interlock supports dead time from the start.** The field exists in
   `interlock_group_t` (default 0 for relays, 200 ms for covers) rather than being
   deferred, because covers need it immediately.

10. **`timer_service` is a 1:1 wrapper over `hal_task_t`**, not a central timer
    list. Each timer keeps its own platform task and adds deadline bookkeeping so
    `restart`, `is_active` and `remaining_ms` are well defined. This avoids a
    shared dispatch list, priority coupling and an extra failure mode, at the cost
    of a few bytes per timer.

11. **Multistate Input Basic is kept unchanged as a state attribute.** The plan
    allowed it as an optional compatibility path; here it is explicitly retained
    with its current value encoding so existing Z2M and ZHA installations keep
    working, while `0xFC02` carries actions.

12. **Button events are sent to the coordinator, not to bindings.** They are
    notifications; control commands (On/Off, Level) continue to go to bindings.
    This requires a new `hal_zigbee_send_cmd_to_coordinator()`.

13. **Diagnostic counters are exposed over Zigbee** (Basic `0xff10`..`0xff15`) in
    addition to the stub REPL, because the hardware regression matrix needs them
    on real devices without a debug probe.

14. **Native C unit tests are added** (`tests/unit/`, `make unit_tests`). The
    existing pytest+stub harness cannot express "inject edges at virtual
    timestamps while the worker is not running", which is exactly the class of bug
    being fixed. Stub REPL additions (`time_advance`, `tasks_poll`, `pin_edge`,
    `diag`) cover the integration-level version of the same scenarios.

15. **NVM migration is required.** Extending the persisted switch and relay config
    structs changes their size, and `hal_nvm_read` is size-exact, so
    `NVM_MIGRATIONS_VERSION` is bumped and those two item families are dropped on
    upgrade.

16. **`boot_press` is explicit.** The current firmware fakes `long_pressed = true`
    at startup to suppress a spurious long press. The replacement is an explicit
    `boot_press` flag consumed by the gesture layer, while raw `UP` is still
    delivered so toggle-type endpoints follow the physical switch position.

17. **No internal compatibility layer.** The original plan kept the old
    `on_press` / `on_release` / `on_long_press` / `on_multi_press` callbacks as an
    adapter for one phase. That is dropped: each stage replaces its predecessor
    outright and deletes it, so no adapter, feature flag, dead branch or
    "previously" comment survives. Only the *external* Zigbee interface keeps
    compatibility guarantees. The migration staging in
    [11_migration.md](./11_migration.md) reflects this — timer service moves ahead
    of the gesture layer so the gesture FSM can land together with the deletion of
    `button.c`.

18. **The stage 6 hardware regression gate is deferred.** No safe bench-accessible
    Telink or Silabs test hardware is available. Stage 7 may proceed through its
    native and stub software gates, but the hardware matrix remains mandatory
    before this refactor is released or merged. The 8 ms debounce default remains
    provisional until that matrix runs.

## Deliberately out of scope

Scenes, power monitoring, curtain-module features, touchlink, wireless-switch
battery work, multi-endpoint gesture aggregation, and persisting any input state
across reboot.
