# 10 — Testing

Three levels, all required:

1. **Native unit tests** (`tests/unit/`, C, host) — FSMs and policies with fully
   controlled clock and task scheduling.
2. **Stub integration tests** (`tests/`, pytest, existing harness) — end-to-end
   behaviour of the whole firmware including Zigbee clusters.
3. **Hardware regression** — mandatory before release; the stub cannot reproduce
   TLSR825x interrupt behaviour.

## Native unit tests

Layout:

```text
tests/unit/
    support/fake_clock.c      # hal_millis(), advance_ms()
    support/fake_tasks.c      # hal_tasks_* with manual poll and injectable latency
    support/fake_gpio.c       # hal_gpio_* + edge injection helper
    support/fake_relay_hw.c   # records relay pin writes with timestamps
    support/runner.h          # assert macros, test registration
    test_gpio_edge_queue.c
    test_button_input.c
    test_gesture_fsm.c
    test_timer_service.c
    test_relay_controller.c
    test_interlock.c
    test_button_event_queue.c
    fuzz_debounce.c           # deterministic PRNG, fixed seed list
```

Built by `make unit_tests` into `build/unit/`, run in CI (`.github/workflows/test.yml`)
before the pytest suite. No external test framework; `runner.h` provides
`TEST(name)`, `ASSERT_EQ`, `ASSERT_TRUE` and an event-log comparison helper.

The fakes give what the stub cannot: injecting edges at arbitrary virtual
timestamps while the worker task is deliberately not run.

### `test_gpio_edge_queue`

| Case | Expectation |
| --- | --- |
| push/pop FIFO order | edges come out in push order |
| fill to capacity | `used == GPIO_EDGE_QUEUE_SIZE`, next push fails, `dropped == 1` |
| overflow does not evict | oldest entries still readable |
| wraparound of head/tail | order preserved over 4 × capacity pushes |
| interleaved push while popping | no torn entries |

### `test_button_input`

| Case | Input (`debounce_ms = 8`) | Expectation |
| --- | --- | --- |
| bounce on press | `DOWN@100, UP@101, DOWN@102, UP@103, DOWN@104`, poll at 120 | exactly one `DOWN`, timestamp 104 |
| very short valid press | `DOWN@100, UP@125`, poll at 140 | `DOWN@100`, `UP@125` |
| worker delayed | `DOWN@0, UP@20, DOWN@40, UP@60`, first poll at 100 | four events in order, two distinct `press_id` |
| release blip | `DOWN@100, UP@110, DOWN@111`, poll at 130 | one `DOWN`, button stays down |
| sub-debounce spike only | `DOWN@100, UP@103`, poll at 200 | no events |
| press_id pairing | 3 press cycles | `press_id` 1,1,2,2,3,3 |
| boot pressed | pin low at init, `UP@50` | no `DOWN`, one `UP`, `boot_press == true` |
| active_high flip | write mode attribute while released | no events, state re-seeded |
| millis wraparound | edges around `0xFFFFFFF0` | events emitted normally |
| two buttons interleaved | edges alternating pins | independent FSMs, global `seq` monotonic |

### `test_gesture_fsm`

| Case | Expectation |
| --- | --- |
| `DOWN, UP`, gap elapses | `N_CLICK(1)` |
| `DOWN, UP, +100 ms, DOWN, UP`, gap elapses | `N_CLICK(2)` only |
| 5 cycles within gaps | `N_CLICK(5)` only |
| 10 cycles within gaps | `N_CLICK(10)` emitted at the 10th `UP`, no timer wait |
| `DOWN, +hold_ms, UP` | `HOLD_START` then `HOLD_END` with correct duration, no `N_CLICK` |
| `DOWN, UP, DOWN, +hold_ms, UP` | `N_CLICK(1)`, `HOLD_START`, `HOLD_END` |
| gap exceeded between clicks | two separate `N_CLICK(1)` |
| `hold_ms = 0` | hold never fires, clicks still counted |
| boot press then `UP` | no gesture |
| release exactly at `hold_ms` | `HOLD_START` + `HOLD_END` (timer wins) |

### `test_timer_service`

`start` on active timer keeps deadline; `restart` replaces it; `cancel` prevents
callback; `remaining_ms` decreases monotonically; callback may restart its own
timer; expiry at exactly the deadline.

### `test_relay_controller`

Driven only by `relay_request_t`; asserts logical state, recorded hardware writes,
timer state and emitted state-change notifications.

| Case | Expectation |
| --- | --- |
| `ON`, `OFF`, `TOGGLE` | state and one hardware write per change |
| repeated `ON` | no duplicate hardware write, no notification |
| `PULSE(500)` | ON now, OFF at +500 |
| `PULSE(500)` then `PULSE(500)` at +300 | OFF at +800 |
| `PULSE(0)` with `inching_ms = 250` | OFF at +250 |
| `PULSE` then explicit `ON` | timer cancelled, relay stays ON |
| `PULSE` then explicit `OFF` | immediate OFF, timer cancelled |
| `ON_TIMED(30000)` | OFF at +30000 |
| `ON_TIMED` then `ON_TIMED` | previous deadline replaced |
| `ON_TIMED` then `TOGGLE` | OFF, timer cancelled |
| auto-off expiry | request re-enters controller with `source = TIMER` |
| latching relay | coil pulse on correct pin, released after 100 ms |
| two latching relays without `SLP` | second pulse deferred by 50 ms |

### `test_interlock`

Group `{R1, R2, R3}`:

| Step | Expectation |
| --- | --- |
| `R1 ON` | R1 on |
| `R2 ON` | R1 off, R2 on |
| `R3 PULSE(500)` | R2 off, R3 on, R3 off at +500 |
| `R1 ON` with `dead_time_ms = 50` | peers off immediately, R1 on at +50 |
| conflicting `R2 ON` during dead time | pending target becomes R2, R1 stays off |
| `OFF` for pending relay during dead time | pending cleared, nothing turns on |
| Zigbee `ON`, gesture `ON`, timer expiry, startup restore | all respect interlock |
| relay outside any group | unaffected |

### `test_button_event_queue`

Ordering, `seq` increments even when dropped, oldest dropped on overflow with
counter increment, TTL discards stale entries at drain, nothing enqueued while not
joined, payload is a snapshot (later state change does not alter a queued entry).

### `fuzz_debounce`

Generator per iteration: intended press sequence (1..12 presses), press duration
20..2000 ms, gap 20..1500 ms, 0..5 bounce edges of 0..5 ms around each transition,
worker latency 0..120 ms, occasional queue overflow. Fixed seed list so failures
are reproducible; seeds are part of the test source.

Invariants checked for every run:

1. Number of committed `DOWN` equals number of intended presses, unless edges
   were dropped (then `dropped > 0` and the test only checks monotonicity).
2. Events strictly alternate `DOWN`, `UP` per button.
3. `seq` strictly increases; `press_id` increases by one per `DOWN`.
4. Every `UP` carries the `press_id` of the preceding `DOWN`.
5. `sum(N_CLICK.count)` plus number of `HOLD_END` equals number of committed
   press cycles that completed within the run.
6. No `N_CLICK(0)` is ever emitted.

## Stub integration tests

New stub REPL commands:

| Command | Meaning |
| --- | --- |
| `time_advance <ms>` | advance the virtual clock without dispatching tasks |
| `tasks_poll` | dispatch due tasks |
| `step_time <ms>` | existing behaviour: advance and dispatch |
| `pin_edge <pin> <0\|1> [after_ms]` | inject an edge, optionally advancing the clock first, without dispatching |
| `diag` | print all diagnostic counters |

New machine events emitted by the stub app (stub-only sinks, no production code
changes): `btn_event id= type= seq= press_id= t=` and
`gesture id= type= count= duration=`.

`tests/conftest.py` changes:

- `DEBOUNCE_MS = 8` to match the new default.
- `press_button` / `release_button` step `DEBOUNCE_MS + 2`.
- new helpers: `inject_edges(pin, [(value, at_ms), ...])`, `click_n(pin, n, gap_ms)`,
  `hold_button(pin, ms)`, `counters()`.

New or expanded test files:

| File | Coverage |
| --- | --- |
| `test_button_events.py` | `DOWN`/`UP`/`HOLD`/`N_CLICK` events over cluster `0xFC02`, `seq` continuity, `press_id` pairing |
| `test_gesture_reset.py` | 10-click factory reset, on-board button hold reset, `MultiPressResetCount = 0` disables both |
| `test_relay_inching.py` | `InchingDuration` attribute, `PULSE` behaviour, restart semantics |
| `test_relay_timed_off.py` | `OnWithTimedOff`, replacement, cancel by explicit `OFF` |
| `test_interlock.py` | interlock across button, Zigbee, pulse, timed-on and toggle |
| `test_input_latency.py` | edge bursts injected while tasks are not polled; exact relay transition count |
| `test_indicator_flash.py` | detached immediate press pulse; no delayed single-click echo; finalised double/triple-click confirmation begins only after `multi_click_gap_ms`; attached-relay indicators remain relay-owned; disconnected and commissioning patterns are not interrupted |

Existing suites must keep passing unchanged in intent:
`test_switch_cluster.py`, `test_switch_cluster_toggle_mode.py`,
`test_switch_cluster_momentary_mode.py`, `test_relay_cluster.py`,
`test_latching_relay.py`, `test_cover_cluster.py`, `test_cover_switch_cluster.py`,
`test_switches_without_relays.py`,
`test_network_join.py`, `test_basic_cluster.py`, `test_battery_cluster.py`,
`test_poll_control_cluster.py`, `test_device_config_parser.py`,
`test_base_components.py`.

## Hardware regression

Required on at least one Telink touch switch (e.g. BSEED TS0726) and one Silabs
device before merging stage 6 and stage 10.

| Test | Method | Pass criterion |
| --- | --- | --- |
| 100 normal clicks | manual or jig, ~1 Hz | 100 relay transitions, `gpio_edges_dropped == 0` |
| 100 fast clicks | as fast as possible | relay transitions == physical clicks |
| bursts of 2, 3, 5, 10 clicks | 10 repetitions each | `N_CLICK` counts match exactly |
| 20 ms / 30 ms / 50 ms clicks | jig or scope-verified taps | every click produces `DOWN` + `UP` |
| hold 1 s / 3 s / 10 s | manual | one `HOLD_START`, one `HOLD_END`, plausible duration |
| 10-click reset | manual | device factory-resets |
| AC hum / noisy input | device under mains load, no touch | zero `button_events_emitted` over 10 minutes |
| end-device sleep wake | press while sleeping | one `DOWN` after wake, no phantom events |

Comparison layers per test: physical clicks, `button_events_emitted`,
`gestures_emitted`, relay transitions, events received by the coordinator.
Counters are read via Basic cluster attributes `0xff10`..`0xff15`.
