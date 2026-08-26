# Input / Relay Pipeline Refactor

Design specification of the **final** shape of the input path (GPIO → button →
gesture → action) and the relay path (request → policy → hardware), plus the
Zigbee event transport for button actions.

Documents describe the target state, not the current code. Migration order and
per-phase acceptance criteria are in [11_migration.md](./11_migration.md);
live progress is tracked in [STATE.md](./STATE.md).

## Documents

| Doc | Content |
| --- | --- |
| [01_architecture.md](./01_architecture.md) | Layers, data flow, ownership rules, system invariants, module map |
| [02_gpio_edge_capture.md](./02_gpio_edge_capture.md) | GPIO HAL edge contract, per-platform capture, edge queue, diagnostics |
| [03_button_input.md](./03_button_input.md) | Debounce FSM, logical events, `press_id`, dispatcher, state queries |
| [04_gesture_fsm.md](./04_gesture_fsm.md) | Hold and N-click semantics, timing, emitted gestures |
| [05_timer_service.md](./05_timer_service.md) | Generic deadline timer API and semantics |
| [06_action_mapper.md](./06_action_mapper.md) | Gesture/event → action mapping, full switch behaviour tables, system actions |
| [07_relay_controller.md](./07_relay_controller.md) | Relay requests, auto-off (inching / timed-off), interlock, latching driver |
| [08_zigbee_events.md](./08_zigbee_events.md) | Button Event cluster, TX queue, sequence numbers, state attributes, HAL fixes |
| [09_configuration.md](./09_configuration.md) | Config string tokens, ZCL config attributes, config/runtime split, NVM |
| [10_testing.md](./10_testing.md) | Unit, property, fuzz, integration and hardware test specification |
| [11_migration.md](./11_migration.md) | Phased migration, compatibility, removals |
| [DECISIONS.md](./DECISIONS.md) | Deviations from the original refactor plan and their rationale |
| [STATE.md](./STATE.md) | Progress tracking board |

## Mental model

```text
GPIO layer captures history.
Button layer converts noisy physical history into clean DOWN/UP events.
Gesture layer converts DOWN/UP history into HOLD and N_CLICK semantics.
Action layer decides what events and gestures mean.
RelayController is the only component allowed to change relays; it owns
interlock, inching and timed-off behaviour.
TimerService is generic infrastructure shared by all owners of deadlines.
Zigbee attribute reporting carries durable state (relay ON/OFF, button state).
Zigbee button actions are immutable events, never an overwritten attribute.
No button, gesture, event or timer runtime state survives a reboot.
```

## Invariants

1. **State vs event** — state answers "what is true now", events answer "what
   happened at a point in time". Events are never reconstructed from state.
2. **Unique press identity** — every accepted physical press has a unique
   `press_id`; the matching UP carries the same `press_id`.
3. **Ordering** — edges and events are processed and delivered in capture order.
4. **No collapsing** — debounce may remove bounce, but a valid `DOWN→UP` cycle
   is never lost because the worker ran late.
5. **Gesture purity** — `N_CLICK(n)` is produced only from `n` completed press
   cycles that did not become holds.
6. **Single relay owner** — every relay transition goes through
   `relay_controller`; nothing else writes relay GPIOs.
7. **Unavoidable interlock** — no code path can reach relay hardware while
   bypassing interlock policy.
8. **Feature-agnostic timers** — `timer_service` knows deadlines and callbacks,
   never feature meaning.
9. **Volatile input state** — nothing in the input pipeline is persisted; only
   configuration is.
10. **Explicit loss** — dropped edges and dropped Zigbee events increment
    counters that are readable over Zigbee and in the stub REPL.
