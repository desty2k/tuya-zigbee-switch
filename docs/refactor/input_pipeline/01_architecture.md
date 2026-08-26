# 01 — Architecture

## Data flow

```text
GPIO hardware
    │  level change
    ▼
hal_gpio edge capture (per platform)
    │  hal_gpio_edge_t {pin, level, timestamp_ms, seq}
    ▼
gpio_edge_queue  (bounded SPSC ring, IRQ producer / task consumer)
    │
    ▼
button_input  (per-button debounce FSM, timestamp based)
    │  button_event_t {button_id, DOWN|UP, timestamp_ms, seq, press_id}
    ▼
button_dispatcher  (synchronous fan-out)
    │
    ├──────────────► immediate consumers
    │                   switch_endpoint       → relay_controller / bindings
    │                   cover_switch_endpoint → cover_controller / bindings
    │
    ├──────────────► gesture_fsm
    │                   │  HOLD_START / HOLD_END / N_CLICK(count)
    │                   ▼
    │                 action_mapper
    │                   ├──► relay_controller
    │                   ├──► binding commands (On/Off, Level)
    │                   ├──► button_event_cluster (Zigbee)
    │                   └──► system_action  (factory reset, reboot)
    │
    └──────────────► button_event_cluster (raw DOWN/UP as Zigbee events)

relay_controller
    ├─ interlock policy (logical relay ids, dead time)
    ├─ auto-off (inching pulse / on-with-timed-off)
    ├─ state change notification → relay endpoint attribute reporting
    └─ relay_driver  ── only writer of relay GPIOs
                         (incl. latching-relay coil pulse sequencing)

timer_service  (generic deadlines)
    ▲            ▲               ▲                 ▲
    │            │               │                 │
gesture_fsm  relay_controller  interlock  cover_controller / led / indicator
```

## Layers and single responsibility

| Layer | Module | Responsibility | Must not contain |
| --- | --- | --- | --- |
| Edge capture | `src/hal/gpio.h` + platform impl | Report every level transition with a timestamp and sequence number | Debounce, buttons, relays, gestures |
| Edge buffer | `src/base_components/gpio_edge_queue.*` | Bounded, immutable, order-preserving edge storage; drop accounting | Any pin semantics |
| Button input | `src/base_components/button_input.*` | Debounce, logical DOWN/UP, `press_id`, current button state | Hold, clicks, relays, Zigbee, reset |
| Dispatch | `src/base_components/button_dispatcher.*` | Ordered synchronous fan-out to sinks | Policy |
| Gesture | `src/base_components/gesture_fsm.*` | HOLD_START, HOLD_END, N_CLICK(count) | Relay access, Zigbee access, reset |
| Action | `src/base_components/action_mapper.*` + cluster code | Meaning of events and gestures | Hardware access, debounce, timing of gestures |
| Relay policy | `src/base_components/relay_controller.*`, `interlock.*` | Requests, interlock, auto-off, state notification | Button semantics, Zigbee framing |
| Relay hardware | `src/base_components/relay_driver.*` | GPIO writes, latching coil pulses | Logical policy, interlock, inching |
| Timers | `src/base_components/timer_service.*` | Deadline + callback | Feature knowledge |
| Zigbee events | `src/zigbee/button_event_cluster.*` | Event framing, TX queue, sequence numbers | Gesture interpretation |
| System actions | `src/device_config/system_action.*` | Factory reset, reboot, scheduled variants | Button knowledge |

## Ownership rules

- `hal_gpio_write` on a relay pin is called **only** from `relay_driver`.
- `relay_driver` functions are called **only** from `relay_controller`.
- `relay_controller` is reached **only** via `relay_ctrl_submit(relay_request_t)`.
- `gesture_fsm` and `button_input` never call Zigbee, relay or reset code.
- `button_input` is the only consumer of `gpio_edge_queue`.
- `timer_service` is the only user of `hal_tasks` outside of HAL and platform code.
- Every deadline belongs to exactly one owner module; timers are stored inside
  the owner's runtime struct.

## Execution model

- One application worker task (`button_input` internal) drains the edge queue,
  advances the debounce FSMs and dispatches events. It is scheduled with delay 0
  by the HAL edge sink and re-armed for the earliest pending debounce deadline.
- The HAL edge sink may be invoked from interrupt context. It performs only a
  ring-buffer push and `hal_tasks_schedule(worker, 0)`.
- Dispatch to sinks, gesture evaluation, action mapping and relay requests all
  run synchronously inside the worker task. No queues exist between application
  layers.
- Queues exist at exactly two boundaries: `IRQ → application`
  (`gpio_edge_queue`) and `application → Zigbee TX`
  (`button_event_cluster` TX ring).

## Module map (final)

```text
src/base_components/
    gpio_edge_queue.c/.h
    button_input.c/.h
    button_dispatcher.c/.h
    gesture_fsm.c/.h
    timer_service.c/.h
    action_mapper.c/.h
    relay_driver.c/.h          (replaces relay.c/.h)
    relay_controller.c/.h
    interlock.c/.h
    led.c/.h                   (unchanged API, timers via timer_service)
    network_indicator.c/.h     (unchanged)
    battery.c/.h               (unchanged)

src/zigbee/
    button_event_cluster.c/.h  (new, cluster 0xFC02)
    switch_cluster.c/.h        (immediate + gesture consumer, no button internals)
    relay_cluster.c/.h         (relay_controller client, attribute reporting)
    cover_cluster.c/.h         (relay_controller client via cover controller logic)
    cover_switch_cluster.c/.h  (immediate + gesture consumer)
    basic_cluster.c/.h         (adds diagnostic counters)

src/device_config/
    config_parser.c/.h         (fills config structs only)
    feature_wiring.c/.h        (new: builds pipeline from parsed config)
    system_action.c/.h         (new: factory reset / reboot actions)

src/hal/
    gpio.h                     (edge sink contract)
    zigbee.h                   (send-to-coordinator, report-attr snapshot fix)
```

`src/base_components/button.c/.h` is removed at the end of the migration.
