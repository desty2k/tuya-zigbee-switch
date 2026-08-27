# 04 — Gesture FSM

Interprets `DOWN`/`UP` sequences of a single button. Emits gestures only. Never
touches relays, Zigbee or reset logic.

## Types

```c
typedef enum {
    GESTURE_HOLD_START,
    GESTURE_HOLD_END,
    GESTURE_N_CLICK,
} gesture_type_t;

typedef struct {
    uint8_t        button_id;
    gesture_type_t type;
    uint8_t        count;         // N_CLICK: number of clicks; otherwise 0
    uint16_t       duration_ms;   // HOLD_END: press duration; otherwise 0
    uint32_t       press_id;      // press that produced the gesture
    uint32_t       timestamp_ms;
} gesture_event_t;

typedef struct {
    uint16_t hold_ms;              // 0 disables hold detection
    uint16_t multi_click_gap_ms;
} gesture_config_t;

typedef struct {
    uint8_t     click_count;
    bool        button_down;
    bool        hold_active;
    bool        suppressed;        // press must not produce gestures
    uint32_t    active_press_id;
    uint32_t    down_at_ms;
    app_timer_t hold_timer;
    app_timer_t multi_click_timer;
} gesture_runtime_t;
```

## Defaults

| Parameter | Default | Range | Notes |
| --- | --- | --- | --- |
| `hold_ms` (switch buttons) | 800 | 0, 100..10000 | existing ZCL attribute `0xff03` |
| `hold_ms` (on-board reset button) | 2000 | 0, 100..10000 | not exposed over Zigbee |
| `multi_click_gap_ms` | 350 | 100..2000 | global `G<N>` config token, per-switch ZCL attribute |
| `GESTURE_MAX_N_CLICK` | 10 | — | compile-time cap |

## Rules

```text
on DOWN(press_id):
    button_down     = true
    active_press_id = press_id
    down_at_ms      = event.timestamp_ms
    hold_active     = false
    suppressed      = button_input_boot_press(button_id) and first press after boot
    timer_cancel(multi_click_timer)
    if hold_ms != 0 and not suppressed:
        app_timer_start(hold_timer, hold_ms)

on hold_timer expiry:
    if click_count > 0:
        emit N_CLICK(click_count)          # sequence terminated by a hold
        click_count = 0
    hold_active = true
    emit HOLD_START(press_id = active_press_id)

on UP(press_id):
    button_down = false
    timer_cancel(hold_timer)
    if suppressed:
        suppressed = false
        return
    if hold_active:
        hold_active = false
        emit HOLD_END(press_id, duration_ms = up.timestamp - down_at_ms)
        return
    click_count++
    if click_count >= GESTURE_MAX_N_CLICK:
        emit N_CLICK(click_count)
        click_count = 0
        return
    app_timer_start(multi_click_timer, multi_click_gap_ms)

on multi_click_timer expiry:
    emit N_CLICK(click_count)
    click_count = 0
```

Consequences:

- `N_CLICK(1)` is emitted `multi_click_gap_ms` after the release. Local relay
  reaction does not wait for it — immediate consumers act on raw `DOWN`/`UP`
  (see [06_action_mapper.md](./06_action_mapper.md)).
- A press that becomes a hold never becomes `N_CLICK(1)`.
- Reaching `GESTURE_MAX_N_CLICK` emits immediately, because the count cannot grow
  further. This keeps the 10-click factory reset instant.
- Clicks accumulated before a hold are flushed as their own `N_CLICK` when the
  hold starts, so `click, click, hold` yields `N_CLICK(2)`, `HOLD_START`,
  `HOLD_END`.
- Hold fires once per press; it does not repeat while the button stays down.
- There is no special casing for 2, 3 or 4 clicks anywhere in the firmware; only
  `count` is carried.

## API

```c
void gesture_fsm_init(void);                                   // registers as button sink
void gesture_fsm_add(uint8_t button_id, const gesture_config_t *cfg);
void gesture_fsm_set_hold_ms(uint8_t button_id, uint16_t hold_ms);
void gesture_fsm_set_multi_click_gap_ms(uint8_t button_id, uint16_t gap_ms);

bool gesture_fsm_hold_active(uint8_t button_id);               // state query

typedef void (*gesture_sink_t)(const gesture_event_t *event, void *arg);
#define GESTURE_SINK_MAX 6
void gesture_fsm_register_sink(gesture_sink_t sink, void *arg);
```

Buttons without a registered gesture config produce no gestures; their raw events
still flow to immediate consumers.

## Examples

| Input | Emitted gestures |
| --- | --- |
| `DOWN, UP`, then gap elapses | `N_CLICK(1)` |
| `DOWN, UP, +100 ms, DOWN, UP`, gap elapses | `N_CLICK(2)` |
| 5 × (`DOWN, UP`) within gaps | `N_CLICK(5)` |
| 10 × (`DOWN, UP`) within gaps | `N_CLICK(10)` at the 10th release |
| `DOWN, +hold_ms, UP` | `HOLD_START`, `HOLD_END(duration)` |
| `DOWN, UP, DOWN, +hold_ms, UP` | `N_CLICK(1)`, `HOLD_START`, `HOLD_END` |
| button already down at boot, then `UP` | none |
