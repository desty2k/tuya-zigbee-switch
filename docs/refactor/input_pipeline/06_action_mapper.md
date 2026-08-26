# 06 — Action layer

Two routers consume the input pipeline:

- **Immediate router** — reacts to raw `DOWN`/`UP` so local control never waits
  for gesture resolution, Zigbee, MQTT or a coordinator.
- **Gesture router (`action_mapper`)** — reacts to `HOLD_START`, `HOLD_END` and
  `N_CLICK(count)`.

Neither router touches hardware directly. Both emit actions.

## Action types

```c
typedef enum {
    ACTION_NONE = 0,
    ACTION_RELAY,          // relay_request_t
    ACTION_BIND_ONOFF,     // On/Off command to bindings
    ACTION_BIND_LEVEL,     // Level Move / Stop to bindings
    ACTION_ZB_BUTTON_EVENT,
    ACTION_SYSTEM,         // system_action_t
} action_type_t;

typedef enum {
    SYSTEM_ACTION_FACTORY_RESET,
    SYSTEM_ACTION_REBOOT,
} system_action_t;
```

`system_action_execute(system_action_t action, uint16_t delay_ms)` lives in
`src/device_config/system_action.c` and is the only path to `hal_factory_reset()`
and `hal_system_reset()` from feature code.

## Switch endpoint behaviour (unchanged semantics)

Configuration attributes per switch endpoint: `mode` (switch type), `action`,
`relay_mode`, `relay_index`, `binded_mode`, `hold_ms` (`0xff03`),
`level_move_rate`.

### Position actions

`relay_index` is valid when `1 <= relay_index <= relay_count`.

| `action` | ON-position relay effect | OFF-position relay effect |
| --- | --- | --- |
| `ONOFF` | relay ON | relay OFF |
| `OFFON` | relay OFF | relay ON |
| `TOGGLE_SIMPLE` | relay TOGGLE | relay TOGGLE |
| `TOGGLE_SMART_SYNC` | relay TOGGLE | relay TOGGLE |
| `TOGGLE_SMART_OPPOSITE` | relay TOGGLE | relay TOGGLE |

| `action` | ON-position and OFF-position binding command |
| --- | --- |
| `ONOFF` | ON-position: `On`; OFF-position: `Off` |
| `OFFON` | ON-position: `Off`; OFF-position: `On` |
| `TOGGLE_SIMPLE` | `Toggle` |
| `TOGGLE_SMART_SYNC` | valid relay: `On` if relay is on else `Off`; no valid relay: `Toggle` |
| `TOGGLE_SMART_OPPOSITE` | valid relay: `Off` if relay is on else `On`; no valid relay: `Toggle` |

Ordering inside one event: relay action first, binding action second, so smart
modes report the relay state *after* the local change. Binding commands are
skipped when the device is not joined.

### Toggle switch type (`mode = TOGGLE`)

| Trigger | Effect |
| --- | --- |
| `DOWN` | flash indicator; if `relay_mode != DETACHED`: ON-position relay effect; ON-position binding command; multistate `presentValue = 3` (POSITION_ON); Zigbee `ButtonEvent(DOWN)` |
| `UP` | flash indicator; if `relay_mode != DETACHED`: OFF-position relay effect; OFF-position binding command; multistate `presentValue = 4` (POSITION_OFF); Zigbee `ButtonEvent(UP)` |
| `HOLD_START` / `HOLD_END` | ignored for relay, bindings and multistate; still emitted as Zigbee events |
| `N_CLICK(n)` | Zigbee event and system-action mapping only |

### Momentary switch types (`mode = MOMENTARY` or `MOMENTARY_NC`)

| Trigger | Effect |
| --- | --- |
| `DOWN` | flash indicator; if `relay_mode == RISE`: ON-position relay effect; if `binded_mode == RISE`: ON-position binding command; multistate `presentValue = 1` (PRESS); Zigbee `ButtonEvent(DOWN)` |
| `HOLD_START` | if `relay_mode == LONG`: relay TOGGLE; if `binded_mode == LONG`: ON-position binding command; `Level Move (with On/Off)` to bindings using `level_move_rate` and the alternating direction; multistate `presentValue = 2` (LONG_PRESS); Zigbee `ButtonEvent(HOLD_START)` |
| `UP` without preceding hold | if `relay_mode == SHORT`: ON-position relay effect; if `binded_mode == SHORT`: ON-position binding command; multistate `presentValue = 0`; Zigbee `ButtonEvent(UP)` |
| `UP` after hold | `Level Stop (with On/Off)` to bindings; multistate `presentValue = 0`; Zigbee `ButtonEvent(UP)`; `HOLD_END` also published |
| `N_CLICK(n)` | Zigbee event and system-action mapping only |

`MOMENTARY_NC` differs only in `active_high = true` for the underlying button.
Writing the switch mode attribute calls
`button_input_set_active_high(button_id, mode == MOMENTARY_NC)`.

`level_move_direction` alternates on every `HOLD_START`, preserving the current
"next long press moves the other way" behaviour.

Indicator flash rules are unchanged: flash only when the endpoint is detached or
its `relay_index` is invalid, only when `blink_times_left == 0`, on `DOWN` for
momentary types and on both `DOWN` and `UP` for toggle type.

### Multistate compatibility attribute

`presentValue` of Multistate Input Basic keeps the existing value set
(`0` not pressed, `1` press, `2` long press, `3` position on, `4` position off)
and stays a *state* attribute. It is derived from the current button and hold
state and is never used to encode click counts. State resynchronisation after a
mode change uses `button_input_is_down()` and `gesture_fsm_hold_active()`.

## Cover switch endpoint behaviour (unchanged semantics)

`cover_switch_cluster` consumes raw `DOWN`/`UP` and `HOLD_START`, and queries
`button_input_is_down()` for both of its buttons, so simultaneous open+close
(stop) detection works exactly as before.

| Trigger | Condition | Present value |
| --- | --- | --- |
| `DOWN` | both buttons down | `STOP` |
| `DOWN` | only open down | `OPEN` (`CLOSE` when `reversal`) |
| `DOWN` | only close down | `CLOSE` (`OPEN` when `reversal`) |
| `HOLD_START` | `switch_type == TOGGLE` | ignored |
| `HOLD_START` | both buttons down | ignored |
| `HOLD_START` | open button holding | `LONG_OPEN` (`LONG_CLOSE` when `reversal`) |
| `HOLD_START` | close button holding | `LONG_CLOSE` (`LONG_OPEN` when `reversal`) |
| `UP` | no button down | `STOP` for toggle type, `RELEASED` for momentary type |
| `UP` | momentary type, other button still down | ignored |
| `UP` | toggle type, open still down | `OPEN` (`CLOSE` when `reversal`) |
| `UP` | toggle type, close still down | `CLOSE` (`OPEN` when `reversal`) |

Present value changes map to local cover commands and binding commands through
the existing `local_mode` / `binded_mode` matrix (`IMMEDIATE`, `SHORT_PRESS`,
`LONG_PRESS`, `HYBRID`). The long-press duration attribute writes `hold_ms` for
both buttons of the endpoint.

## Gesture mapping table

`action_mapper` holds a static table built during wiring. Lookup key is
`(button_id, gesture_type, count)`.

| Key | Action |
| --- | --- |
| on-board button (`B` token), `HOLD_START`, `hold_ms = 2000` | `SYSTEM_ACTION_FACTORY_RESET` |
| any switch button (`S` token) or cover switch button (`X` token), `N_CLICK(n)` with `reset_count != 0` and `n >= reset_count` | `SYSTEM_ACTION_FACTORY_RESET` |
| any button, `HOLD_START` / `HOLD_END` / `N_CLICK(n)` | `ACTION_ZB_BUTTON_EVENT` |

`reset_count` is the existing global `g_multi_press_reset_count`
(Basic cluster attribute `0xff02`, default 10, `0` disables). The mapper never
hardcodes "2 clicks = X"; every mapping is a table entry.

Rules for the mapper:

- It contains no timing logic; timing belongs to `gesture_fsm`.
- It performs no hardware access; it submits `relay_request_t`, binding commands,
  Zigbee events or system actions.
- Unmapped gestures are still published as Zigbee events.

## Relation between immediate actions and N-click

Relay and binding behaviour is driven by raw press cycles. A double click on a
switch configured with `relay_mode = SHORT` toggles the relay twice **and**
publishes `N_CLICK(2)`. This is intentional: local control latency is never
traded for gesture resolution. Automations that need "double click" without local
toggling use `relay_mode = DETACHED`.
