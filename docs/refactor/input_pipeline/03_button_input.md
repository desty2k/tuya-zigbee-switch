# 03 — Button input layer

Converts raw edge history into clean logical events. Contains debounce and
nothing else: no hold, no clicks, no relays, no Zigbee, no factory reset.

## Types

```c
typedef enum {
    BUTTON_STATE_UP = 0,
    BUTTON_STATE_DOWN = 1,
} button_state_t;

typedef enum {
    BUTTON_EVENT_DOWN,
    BUTTON_EVENT_UP,
} button_event_type_t;

typedef struct {
    uint8_t             button_id;
    button_event_type_t type;
    uint32_t            timestamp_ms;  // time of the accepted transition
    uint32_t            seq;           // monotonic across all buttons
    uint32_t            press_id;      // identity of the press cycle
} button_event_t;

typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        active_high;   // 1 => DOWN when pin level is 1
    uint16_t       debounce_ms;
} button_config_t;

typedef struct {
    button_state_t stable_state;  // last committed logical state
    button_state_t raw_state;     // last observed (uncommitted) logical state
    uint32_t       raw_since_ms;  // timestamp of the edge that started raw_state
    uint32_t       press_id;      // incremented on every accepted DOWN
    bool           boot_press;    // button was DOWN when the pipeline started
} button_runtime_t;
```

`button_config_t` and `button_runtime_t` are stored in separate arrays indexed by
`button_id`.

## Defaults

| Parameter | Default | Range | Source |
| --- | --- | --- | --- |
| `debounce_ms` | 8 | 1..200 | global `D<N>` config token, per-button ZCL attribute |
| queue size | 32 edges | — | `GPIO_EDGE_QUEUE_SIZE` |

## Debounce algorithm

Debounce works on edge timestamps, never on scheduler latency. A raw state
segment is committed when it is known to have lasted at least `debounce_ms` —
either because a later edge proves it, or because the current time proves it.

```text
process_edge(e):                       # edges consumed in order
    new_state = (e.level == active_high) ? DOWN : UP
    if new_state == raw_state:
        return                          # duplicate level, ignore
    if raw_state != stable_state and (e.timestamp - raw_since_ms) >= debounce_ms:
        commit(raw_state, raw_since_ms)
    raw_state    = new_state
    raw_since_ms = e.timestamp

tick(now):                              # after the queue is drained
    if raw_state != stable_state:
        if (now - raw_since_ms) >= debounce_ms:
            commit(raw_state, raw_since_ms)
        else:
            arm worker at raw_since_ms + debounce_ms

commit(state, at):
    stable_state = state
    if state == DOWN:
        press_id++
        emit {DOWN, timestamp = at, press_id}
    else:
        emit {UP, timestamp = at, press_id}
```

Properties:

- Bounce is filtered: `DOWN@100, UP@101, DOWN@102, UP@103, DOWN@104` with
  `debounce_ms = 8` commits exactly one DOWN (timestamped 104).
- Short valid presses survive: `DOWN@100, UP@125` commits DOWN@100 and UP@125
  even if the worker first runs at `t = 150`.
- A short release blip during a press is absorbed: `DOWN@100, UP@110, DOWN@111`
  commits DOWN@100 and leaves the button DOWN.
- Time comparisons use unsigned subtraction, so `hal_millis()` wraparound at
  2^32 ms is handled without special cases.

## Press identity

`press_id` starts at 0 and increments on every committed DOWN. The following UP
carries the same value. Consumers never need to ask whether `is_down == true`
refers to the first or a later press; the pair `(button_id, press_id)` identifies
a press cycle uniquely until wraparound.

## Boot behaviour

```text
button_input_add(config):
    level        = hal_gpio_read(pin)
    stable_state = raw_state = level_to_state(level)
    raw_since_ms = hal_millis()
    press_id     = 0
    boot_press   = (stable_state == DOWN)
    hal_gpio_watch_pin(pin)
```

No synthetic DOWN, UP or click is generated at startup. Pending gestures and the
edge queue are empty. `boot_press` lets the gesture layer suppress hold and click
interpretation for a press that was already active before boot; the eventual UP
is still emitted as a normal event so that toggle-type switch endpoints can
follow the physical position.

## API

```c
void    button_input_init(void);                       // registers HAL edge sink + worker
uint8_t button_input_add(const button_config_t *cfg);  // returns button_id
void    button_input_set_active_high(uint8_t button_id, bool active_high);
void    button_input_set_debounce_ms(uint8_t button_id, uint16_t debounce_ms);

bool    button_input_is_down(uint8_t button_id);        // state query
bool    button_input_boot_press(uint8_t button_id);
uint8_t button_input_count(void);
```

`button_input_set_active_high()` re-reads the pin and re-seeds `stable_state`
and `raw_state` without emitting events, so switching a switch endpoint between
momentary-NO and momentary-NC does not fabricate a press.

## Dispatcher

```c
typedef void (*button_event_sink_t)(const button_event_t *event, void *arg);

#define BUTTON_SINK_MAX 6
void button_dispatcher_register(button_event_sink_t sink, void *arg);
void button_dispatcher_emit(const button_event_t *event);
```

- Sinks are called synchronously, in registration order, from the worker task.
- Registration order is fixed by wiring: immediate consumers first, then
  `gesture_fsm`, then the Zigbee publisher. This guarantees local relay reaction
  precedes any network work for the same event.
- Sinks must not block and must not re-enter `button_dispatcher_emit`.

## Worker scheduling

```text
worker():
    while gpio_edge_queue_pop(&edge):
        route edge to the button whose pin matches edge.pin
        process_edge(edge)
    now = hal_millis()
    for each button: tick(now)
    if any button has a pending debounce deadline:
        hal_tasks_schedule(worker, earliest_deadline - now)
```

The worker is also kicked with delay 0 by the HAL edge sink. Gesture and relay
deadlines are owned by their modules through `timer_service` and do not affect
worker scheduling.
