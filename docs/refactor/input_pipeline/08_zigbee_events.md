# 08 — Zigbee button events and state

User actions are immutable events sent as cluster commands. Durable state stays
in attributes. No action is ever encoded by overwriting a mutable attribute.

## Button Event cluster `0xFC02`

Server cluster, added to every switch endpoint and every cover switch endpoint.

### Command `0x00` ButtonEvent (server → client)

```c
typedef enum {
    ZB_BUTTON_DOWN       = 0,
    ZB_BUTTON_UP         = 1,
    ZB_BUTTON_HOLD_START = 2,
    ZB_BUTTON_HOLD_END   = 3,
    ZB_BUTTON_N_CLICK    = 4,
} zb_button_event_type_t;

typedef struct {
    uint16_t seq;          // per-device event sequence number
    uint16_t press_id;     // low 16 bits of button_input press_id
    uint8_t  event_type;
    uint8_t  count;        // N_CLICK: click count; otherwise 0
    uint16_t duration_ms;  // HOLD_END: press duration; otherwise 0
} zb_button_event_payload_t;   // 8 bytes, little endian
```

The payload is a snapshot copied at enqueue time. Nothing points at live state.

### Attributes

| Id | Name | Type | Access | Meaning |
| --- | --- | --- | --- | --- |
| `0x0000` | `ButtonState` | enum8 | read, reportable | `0` released, `1` pressed — current physical state, diagnostics only |
| `0x0001` | `LastEventSeq` | uint16 | read, reportable | `seq` of the last event handed to the stack |
| `0x0002` | `MultiClickGap` | uint16 | read/write | `multi_click_gap_ms` for this endpoint's button |
| `0x0003` | `DebounceMs` | uint16 | read/write | `debounce_ms` for this endpoint's button |

`ButtonState` must never be treated as the source of user actions.

### Legacy transport

Multistate Input Basic (`0x0012`) keeps its current attribute set and value
encoding as a compatibility state attribute. It is not extended with click
counts. Both models coexist; converters prefer `0xFC02` commands when present.

## Sequence numbers

- One counter per device, `uint16_t`, incremented for every event handed to the
  TX queue, including events that are later dropped.
- Gaps let a coordinator detect loss: receiving `123, 125` proves `124` was lost.
- `press_id` is carried alongside so a consumer can group `DOWN`, `UP`,
  `HOLD_START`, `HOLD_END` of the same press cycle.

## TX queue

```c
#define ZB_BUTTON_EVENT_QUEUE_SIZE 16
#define ZB_BUTTON_EVENT_TTL_MS     5000

typedef struct {
    uint8_t                   endpoint;
    uint32_t                  enqueued_at_ms;
    zb_button_event_payload_t payload;
} zb_button_event_entry_t;
```

Rules:

- Volatile, bounded, never persisted.
- Not joined to a network → the event is not enqueued; only the counters advance.
- Queue full → the oldest entry is dropped and `zb_button_events_dropped`
  increments; the newest event is always kept.
- Entries older than `ZB_BUTTON_EVENT_TTL_MS` are discarded at transmission time,
  so a device that reconnects after 30 minutes does not replay stale clicks.
- Drain is triggered on enqueue and on network-joined transitions.
- Local relay behaviour is completely independent of this queue.

## Transmission

Button events are sent to the coordinator, not to the binding table, because they
are notifications rather than control commands. On/Off and Level commands
produced by switch endpoints continue to be sent to bindings.

```c
hal_zigbee_status_t hal_zigbee_send_cmd_to_coordinator(const hal_zigbee_cmd *cmd);
```

- Telink: `dstAddrMode = APS_SHORT_DSTADDR_WITHEP`, `shortAddr = 0x0000`,
  `dstEp = 1`.
- Silabs: unicast to node id `0x0000`, endpoint 1.
- Stub: emits a `zcl_cmd_send` machine event with `dst=coordinator`, so tests
  observe it exactly like binding commands.

## HAL corrections

- `hal_zigbee_send_report_attr(endpoint, cluster, attr, type, value, len)` must
  transmit the **passed** `value` snapshot. The Telink implementation currently
  ignores it and re-reads the live attribute table, which coalesces rapid changes.
  Telink and Silabs implementations must behave identically after the fix.
- `hal_zigbee_notify_attribute_changed()` stays the trigger for ordinary state
  reporting.

## Zigbee2MQTT converter

Generated converter exposes generic and convenience forms:

| Payload | Exposed |
| --- | --- |
| `event_type = N_CLICK`, `count = n` | `action: "multi_click"`, `action_count: n`, plus `action: "single" / "double" / "triple" / "quadruple"` for `n` in 1..4 |
| `event_type = DOWN` | `action: "press"` |
| `event_type = UP` | `action: "release"` |
| `event_type = HOLD_START` | `action: "hold"` |
| `event_type = HOLD_END` | `action: "hold_release"`, `action_duration: duration_ms` |

`seq` is used for duplicate suppression and gap detection. The firmware side
carries only `count`; it never enumerates click names.

## Diagnostic counters over Zigbee

Basic cluster, read-only `uint32`, for field debugging:

| Id | Counter |
| --- | --- |
| `0xff10` | `gpio_edges_captured` |
| `0xff11` | `gpio_edges_dropped` |
| `0xff12` | `button_events_emitted` |
| `0xff13` | `gestures_emitted` |
| `0xff14` | `zb_button_events_dropped` |
| `0xff15` | `gpio_rearm_limit_hits` |
