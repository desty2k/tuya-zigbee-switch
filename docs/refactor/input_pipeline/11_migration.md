# 11 — Migration

No big-bang rewrite. Each phase is independently shippable, keeps user-visible
behaviour intact unless stated, and is gated by tests.

Implementation order:

```text
1 GPIO edge correctness
2 edge queue
3 debounce FSM
4 compatibility adapter for existing callbacks
5 hardware regression
6 gesture FSM / N-click
7 timer service
8 relay controller
9 inching + timed-off
10 interlock
11 Zigbee event transport
12 removal of the old coupled button logic
```

## Phase 1 — GPIO edge capture

Scope: `hal_gpio_edge_t`, `hal_gpio_set_edge_sink`, `hal_gpio_watch_pin`,
capture procedure on Telink, Silabs and stub. HAL counters.
Old `hal_gpio_callback` remains temporarily as a thin wrapper over the sink so
nothing else has to change yet.
Gate: existing pytest suite green; `gpio_edges_captured` visible in the stub.

## Phase 2 — Edge queue

Scope: `gpio_edge_queue`, sink pushes into it, worker task drains it and calls the
legacy per-pin callbacks in edge order.
Gate: `test_gpio_edge_queue` unit tests; existing pytest suite green.

## Phase 3 — Debounce FSM

Scope: `button_input` with the timestamp-based commit algorithm, `press_id`,
`button_dispatcher`. Default `debounce_ms` becomes 8.
Gate: `test_button_input`, `fuzz_debounce`, updated `conftest.DEBOUNCE_MS`,
`test_input_latency.py`.

## Phase 4 — Compatibility adapter

Scope: an adapter sink translating `button_event_t` into the existing
`on_press` / `on_release` / `on_long_press` / `on_multi_press` callbacks, with hold
and multi-press timing temporarily kept inside the adapter.
`switch_cluster`, `cover_switch_cluster` and `config_parser` are untouched.
Gate: full pytest suite green with zero test changes other than debounce timing.

## Phase 5 — Hardware regression

Scope: no new features. Run the hardware matrix from
[10_testing.md](./10_testing.md) and tune `debounce_ms` if needed.
Gate: all hardware criteria met on Telink and Silabs. Nothing proceeds until the
input path is proven on real devices.

## Phase 6 — Gesture FSM and N-click

Scope: `gesture_fsm` with hold and N-click, `action_mapper` skeleton,
`system_action`. Factory reset is rewired from the old multi-press callback to
`N_CLICK(>= reset_count)`, and the on-board button reset to `HOLD_START`.
The adapter stops synthesising hold and multi-press; `switch_cluster` and
`cover_switch_cluster` consume `HOLD_START` / `HOLD_END` for their long-press
behaviour.
Gate: `test_gesture_fsm`, `test_gesture_reset.py`, momentary and toggle switch
suites unchanged.

## Phase 7 — Timer service

Scope: `timer_service`; migrate gesture timers, LED blink, cover movement delay,
latching coil pulse and reset scheduling onto it. No behaviour change.
Gate: `test_timer_service`; full pytest suite green.

## Phase 8 — Relay controller

Scope: `relay_driver` + `relay_controller` + `relay_request_t`. `relay_cluster`,
`cover_cluster` and switch endpoints stop calling `relay_on/off/toggle` and submit
requests instead. Startup restore goes through the controller.
Gate: `test_relay_controller`, `test_relay_cluster.py`, `test_latching_relay.py`,
`test_cover_cluster.py` unchanged.

## Phase 9 — Inching and timed-off

Scope: unified `auto_off_timer`, `RELAY_REQUEST_PULSE`, `RELAY_REQUEST_ON_TIMED`,
`InchingDuration` attribute, `OnWithTimedOff` command support.
Gate: `test_relay_inching.py`, `test_relay_timed_off.py`.

## Phase 10 — Interlock

Scope: `interlock` groups, `K` config token, `InterlockGroup` attribute, implicit
cover group with 200 ms dead time.
Gate: `test_interlock`, `test_interlock.py`, cover suites unchanged.

## Phase 11 — Zigbee event transport

Scope: cluster `0xFC02`, TX queue, sequence numbers,
`hal_zigbee_send_cmd_to_coordinator`, `hal_zigbee_send_report_attr` snapshot fix,
diagnostic counters in Basic, Z2M converter and ZHA quirk regeneration.
Multistate Input Basic reporting stays as-is.
Gate: `test_button_events.py`, hardware verification of received events, second
hardware regression pass.

## Phase 12 — Removal

Scope: delete `src/base_components/button.c/.h` and the compatibility adapter;
remove `hal_gpio_callback` / `hal_gpio_unreg_callback`; move remaining direct
`hal_tasks` users onto `timer_service`; delete `on_multi_press` plumbing from
`config_parser`.
Gate: no references to removed symbols; full unit + pytest + hardware suites green.

## Compatibility guarantees

- Existing Zigbee attributes keep their ids, types and meanings; only additive
  changes are made.
- Multistate Input Basic keeps working for existing Z2M and ZHA installations.
- Default behaviour of `press_start` / `short_press` / `long` relay modes,
  switch actions and binding modes is unchanged.
- Factory reset entry points remain: on-board button hold and N consecutive
  clicks on a switch button, with the same `MultiPressResetCount` attribute.
- OTA updates from any older firmware version remain possible; the NVM migration
  described in [09_configuration.md](./09_configuration.md) resets only the switch
  and relay endpoint config items.

## Behaviour changes users can notice

| Change | Effect |
| --- | --- |
| `debounce_ms` default 50 → 8 | faster reaction; noisy hardware can restore a higher value with `D<N>` or the per-endpoint attribute |
| Multi-press gap default 800 → 350 ms | click sequences must be a bit faster; `N_CLICK(1)` latency drops accordingly |
| Reset by clicks fires after the sequence resolves | with the default threshold of 10 (equal to `GESTURE_MAX_N_CLICK`) it still fires on the 10th click, with no added delay |
| A press that becomes a hold no longer counts as a click | holds and clicks stop interfering |
| New `0xFC02` events | additional actions available in Z2M; existing actions unchanged |
