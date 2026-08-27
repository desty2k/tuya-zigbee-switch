# 11 — Migration

Staged, but without a compatibility layer. Each stage replaces the old
implementation outright: no adapters, no parallel code paths, no dead code left
behind. A stage is done when the old symbols it replaces no longer exist.

Implementation order:

```text
1  GPIO edge capture
2  Edge queue + worker
3  Timer service
4  Button input (debounce, press_id, dispatcher)
5  Gesture FSM + action mapper; old button layer deleted
6  Hardware regression
7  Relay driver + relay controller
8  Inching + timed-off
9  Interlock
10 Zigbee button event transport
```

## Stage 1 — GPIO edge capture

Scope: `hal_gpio_edge_t`, `hal_gpio_set_edge_sink`, `hal_gpio_watch_pin`,
`hal_gpio_unwatch_pin`; capture procedure on Telink, Silabs and stub; HAL edge
counters. `hal_gpio_callback` and `hal_gpio_unreg_callback` are deleted; the only
current caller (`btn_init`) is converted to the sink, keeping its existing
debounce logic for one stage.
Gate: full pytest suite green; `gpio_edges_captured` non-zero in the stub.

## Stage 2 — Edge queue and worker

Scope: `gpio_edge_queue`, the sink pushes edges, a single worker task drains them
in order. The per-pin callback fan-out inside the old button code is driven from
the worker instead of from the HAL.
Gate: `test_gpio_edge_queue` unit tests; pytest suite green.

## Stage 3 — Timer service

Scope: `timer_service`; every existing deadline user is migrated to it
(`led` blink, cover movement delay, reset/reboot scheduling, latching coil pulse,
old button timing). Direct `hal_tasks` use outside HAL and the input worker is
removed.
Gate: `test_timer_service`; pytest suite green; no behaviour change.

## Stage 4 — Button input

Scope: `button_input` with the timestamp-based commit algorithm, `press_id`,
`button_dispatcher`. Default `debounce_ms` becomes 8. Consumers are still the old
cluster callbacks, now driven from dispatcher sinks.
Gate: `test_button_input`, `fuzz_debounce`, `test_input_latency.py`, updated
`conftest.DEBOUNCE_MS`, pytest suite green.

## Stage 5 — Gesture layer and old button removal

Scope: `gesture_fsm`, `action_mapper`, `system_action`. `switch_cluster` and
`cover_switch_cluster` are rewritten as event and gesture consumers. Factory reset
becomes a system action driven by `HOLD_START` (on-board button) and
`N_CLICK(>= reset_count)` (switch and cover switch buttons).
`src/base_components/button.c/.h`, `on_press` / `on_release` / `on_long_press` /
`on_multi_press` and the callback wiring in `config_parser` are deleted;
`feature_wiring` takes over pipeline construction.
Gate: `test_gesture_fsm`, `test_gesture_reset.py`, full pytest suite green, no
references to the removed symbols.

## Stage 6 — Hardware regression

Scope: no new code. Run the hardware matrix from
[10_testing.md](./10_testing.md) on Telink and Silabs, finalise `debounce_ms`.
Gate: all hardware criteria met. When safe test hardware is unavailable, record
the stage as blocked and keep `debounce_ms` provisional; later software stages
may proceed, but the refactor cannot be released or merged until this gate passes.

## Stage 7 — Relay driver and controller

Scope: `relay_driver` + `relay_controller` + `relay_request_t`. `relay.c/.h` is
deleted. `relay_cluster`, `cover_cluster` and switch endpoints submit requests
instead of calling relay functions. Startup restore goes through the controller.
Gate: `test_relay_controller`, `test_relay_cluster.py`, `test_latching_relay.py`,
`test_cover_cluster.py`, `test_cover_switch_cluster.py`.

## Stage 8 — Inching and timed-off

Scope: unified `auto_off_timer`, `RELAY_REQUEST_PULSE`, `RELAY_REQUEST_ON_TIMED`,
`InchingDuration` attribute, `OnWithTimedOff` command.
Gate: `test_relay_inching.py`, `test_relay_timed_off.py`.

## Stage 9 — Interlock

Scope: `interlock` groups, `K` config token, `InterlockGroup` attribute, implicit
cover group with 200 ms dead time.
Gate: `test_interlock` unit and integration suites; cover suites unchanged.

## Stage 10 — Zigbee button event transport

Scope: cluster `0xFC02`, TX queue, sequence numbers,
`hal_zigbee_send_cmd_to_coordinator`, `hal_zigbee_send_report_attr` snapshot fix,
diagnostic counters in Basic, regenerated Z2M converters and ZHA quirk.
Multistate Input Basic stays as a state attribute.
Gate: `test_button_events.py`, second hardware regression pass, received events
verified in Z2M.

## Interface compatibility

Zigbee interface compatibility is preserved because it is an external contract:

- Existing attribute ids, types and semantics are unchanged; new attributes and
  the new cluster are additive.
- Multistate Input Basic keeps its current value encoding.
- Relay modes, switch actions and binding modes keep their behaviour.
- Both factory reset entry points survive, with the same
  `MultiPressResetCount` attribute.
- OTA from any released version stays possible; the NVM migration in
  [09_configuration.md](./09_configuration.md) drops only the switch and relay
  endpoint config items.

Internal firmware APIs carry no compatibility guarantee. Replaced code is
deleted in the stage that replaces it.

## Behaviour changes users can notice

| Change | Effect |
| --- | --- |
| `debounce_ms` default 50 → 8 | faster reaction; noisy hardware can set a higher value with `D<N>` or the per-endpoint attribute |
| Multi-click gap default 800 → 350 ms | click sequences must be slightly faster; `N_CLICK(1)` latency drops accordingly |
| Reset by clicks resolves as a gesture | with the default threshold of 10 (equal to `GESTURE_MAX_N_CLICK`) it still fires on the 10th click with no added delay |
| A press that becomes a hold no longer counts as a click | holds and clicks stop interfering |
| New `0xFC02` events | extra actions in Z2M; existing actions unchanged |
