# 07 — Relay controller

The only owner of relay state. Every relay change from any source passes through
it, so interlock and auto-off cannot be bypassed.

## Requests

```c
typedef enum {
    RELAY_REQUEST_ON,
    RELAY_REQUEST_OFF,
    RELAY_REQUEST_TOGGLE,
    RELAY_REQUEST_PULSE,      // ON now, OFF after duration (inching)
    RELAY_REQUEST_ON_TIMED,   // ON now, OFF after duration (on-with-timed-off)
} relay_request_type_t;

typedef enum {
    RELAY_SOURCE_BUTTON,
    RELAY_SOURCE_GESTURE,
    RELAY_SOURCE_ZIGBEE,
    RELAY_SOURCE_TIMER,
    RELAY_SOURCE_INTERLOCK,
    RELAY_SOURCE_STARTUP,
    RELAY_SOURCE_COVER,
} relay_request_source_t;

typedef struct {
    uint8_t                relay_id;
    relay_request_type_t   type;
    uint32_t               duration_ms;  // 0 = use configured default
    relay_request_source_t source;
} relay_request_t;

hal_zigbee_cmd_result_t relay_ctrl_submit(const relay_request_t *req);
bool                    relay_ctrl_is_on(uint8_t relay_id);
uint8_t                 relay_ctrl_count(void);
```

## Configuration and runtime

```c
typedef struct {
    uint16_t inching_ms;       // default PULSE duration, 0 = inching disabled
    uint8_t  interlock_group;  // 0 = none
} relay_config_t;

typedef enum {
    AUTO_OFF_NONE,
    AUTO_OFF_PULSE,
    AUTO_OFF_TIMED,
} auto_off_reason_t;

typedef struct {
    bool              is_on;
    auto_off_reason_t auto_off_reason;
    app_timer_t       auto_off_timer;
    relay_driver_t *  driver;
    relay_state_cb_t  on_change;
    void *            cb_param;
} relay_runtime_t;
```

`AUTO_OFF_PULSE` and `AUTO_OFF_TIMED` share one deadline mechanism; they differ
only in configuration source and reported reason.

## Request handling

```text
relay_ctrl_submit(req):
    resolve target = TOGGLE ? (is_on ? OFF : ON) : req.type
    if target turns the relay ON:
        interlock_prepare(relay_id)      # may defer the ON, see below
    apply(target)
    update auto_off state per the table
    if state changed: driver write, on_change(relay_id, is_on)
```

### Auto-off and cancellation policy

| Current auto-off | Request | Result |
| --- | --- | --- |
| any | `OFF` | relay OFF, timer cancelled, reason `NONE` |
| any | `ON` (source ≠ `TIMER`) | relay ON, timer cancelled, reason `NONE` |
| any | `TOGGLE` | resolves to `ON` or `OFF` above |
| any | `ON_TIMED(d)` | relay ON, timer restarted with `d`, reason `TIMED` |
| any | `PULSE(d)` | relay ON, timer restarted with `d` or `inching_ms`, reason `PULSE` |
| `PULSE` / `TIMED` | timer expiry | internal `relay_ctrl_submit(OFF, source = TIMER)` |

Consequences: an explicit `ON` cancels a pending auto-off; an explicit `OFF`
cancels everything; a new timed request replaces the previous one; a new pulse
restarts the pulse (pulse 500 ms, then another pulse 500 ms after 300 ms results
in OFF at 800 ms).

Timer expiry never writes hardware directly — it re-enters the controller so
policies and interlock stay in one place.

### Zigbee mapping

| Command / attribute | Request |
| --- | --- |
| On/Off `On` (0x01), `OnWithRecallGlobalScene` (0x41) | `ON` |
| On/Off `Off` (0x00), `OffWithEffect` (0x40) | `OFF` |
| On/Off `Toggle` (0x02) | `TOGGLE` |
| On/Off `OnWithTimedOff` (0x42) | `ON_TIMED(on_time × 100 ms)`; `off_wait_time` is not implemented |
| Level `MoveToLevelWithOnOff` | level 0 → `OFF`, otherwise `ON` |
| On/Off `InchingDuration` attribute (`0xff03`) | writes `inching_ms` |
| On/Off `InterlockGroup` attribute (`0xff04`) | writes `interlock_group` |

Startup behaviour (`StartUpOnOff`, `0x4003`) is applied at boot as
`relay_ctrl_submit(..., source = RELAY_SOURCE_STARTUP)`; `TOGGLE` and
`PREVIOUS` modes keep persisting the relay state on change, exactly as today.

## Interlock

```c
typedef struct {
    uint8_t     group_id;
    uint16_t    relay_mask;      // bit i = relay_id i, up to 16 relays
    uint16_t    dead_time_ms;    // 0 = switch peers off and target on immediately
    uint8_t     pending_on_relay;
    app_timer_t dead_time_timer;
} interlock_group_t;
```

`interlock_prepare(relay_id)` before every ON transition:

```text
group = group_of(relay_id)
if group == NULL: return ALLOW
for each peer in group.relay_mask, peer != relay_id, relay_ctrl_is_on(peer):
    cancel peer auto_off timer
    submit OFF for peer with source = RELAY_SOURCE_INTERLOCK
if group.dead_time_ms == 0: return ALLOW
group.pending_on_relay = relay_id
timer_restart(group.dead_time_timer, group.dead_time_ms)
return DEFER
```

Rules:

- Interlock operates on logical relay ids, never on GPIO pins.
- A relay belongs to at most one group.
- ON of a deferred target is applied by the group's dead-time callback through
  `relay_ctrl_submit(ON, source = RELAY_SOURCE_INTERLOCK)`; a conflicting request
  arriving during the dead time replaces `pending_on_relay`, and an `OFF` for the
  pending relay clears it.
- Applies to every source: physical button, gesture, Zigbee command, binding,
  timer expiry, pulse, startup restore and cover movement.
- Cover endpoints register their open/close relay pair as an implicit interlock
  group with `dead_time_ms = 200` (the existing `RELAY_MIN_SWITCH_TIME_MS`), so
  the two motor directions cannot be energised together even if a future code
  path forgets the cover state machine. The cover cluster keeps its own movement
  sequencing and `moving` attribute and issues requests with
  `source = RELAY_SOURCE_COVER`.

## Relay driver

`relay_driver` replaces the current `relay.c` and is the only writer of relay
GPIOs.

```c
typedef struct {
    hal_gpio_pin_t on_pin;
    hal_gpio_pin_t off_pin;      // latching relays only
    uint8_t        on_high;
    uint8_t        is_latching;
    app_timer_t    latch_timer;
} relay_driver_t;

void relay_driver_init(relay_driver_t *d);
void relay_driver_apply(relay_driver_t *d, bool on);
```

- Non-latching relays: continuous drive of `on_pin`.
- Latching (bi-stable) relays: a coil pulse of `RELAY_LATCH_PULSE_MS` (100 ms) on
  `on_pin` or `off_pin`. While another relay's coil pulse is active, the pulse is
  retried after `RELAY_LATCH_WAIT_MS` (50 ms), unless the global
  `allow_simultaneous_latching_pulses` (`SLP` config token) is set.
- The latching coil pulse is a hardware detail and is unrelated to
  `RELAY_REQUEST_PULSE` (inching). The two never share a timer or a name.
- `relay_driver` reports nothing and knows no policy.

## State reporting

```c
typedef void (*relay_state_cb_t)(void *param, uint8_t relay_id, bool is_on);
```

`relay_cluster` registers the callback and, on every change, updates the On/Off
attribute, notifies attribute change for reporting, synchronises its indicator LED
and persists the state when `StartUpOnOff` is `TOGGLE` or `PREVIOUS`. Relay state
is durable state, so attribute reporting is the correct transport for it.
