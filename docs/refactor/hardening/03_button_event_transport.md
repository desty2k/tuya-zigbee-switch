# 03 — Paced `0xFC02` button-event transport

Phase 1, priority 4 in implementation order. This is the transport acceptance
path for upstream issue 438.

## Failure model

`button_event_cluster_drain()` currently loops until the queue is empty. The
Telink send path discards the return from `zcl_sendCmd()` and reports successful
submission. A burst can therefore be handed to the stack in one call chain and
removed from firmware ownership before the stack has room to process it.

Local relay correctness and event transport remain independent. A relay action
may succeed while its notification is retried, expires or is dropped, and the
corresponding counter must make that outcome visible.

## Queue

Keep immutable entries and the existing capacity and TTL:

```c
#define ZB_BUTTON_EVENT_QUEUE_SIZE       16
#define ZB_BUTTON_EVENT_TTL_MS           5000
#define ZB_BUTTON_EVENT_TX_INTERVAL_MS     40
#define ZB_BUTTON_EVENT_RETRY_MS          100
```

Queue policy:

- not joined at event creation: do not enqueue;
- full queue: evict the oldest entry, increment `dropped`, enqueue newest;
- expired front: remove it, increment `expired`, then schedule another pump;
- failed submission: retain the front entry and increment `send_failed`;
- successful submission: update `LastEventSeq`, remove the front entry and wait
  at least `TX_INTERVAL_MS` before the next entry;
- every enqueue updates `high_water = max(high_water, used)`.

## TX pump

One `timer_service_t` or equivalent owned task drives the queue. A callback does
at most one stack submission.

```text
enqueue:
    append immutable entry
    if joined and pump idle: schedule pump now

pump:
    if not joined or queue empty: stop
    if front expired:
        pop; expired++
        schedule now; return
    status = send(front)
    if status == OK:
        pop; submitted++
        schedule TX_INTERVAL_MS if queue nonempty
    else:
        send_failed++
        schedule min(RETRY_MS, time until front expires)
```

An expiry-only callback still removes at most one item. This preserves a strict
bounded-work rule even after a long coordinator outage.

## HAL submission contract

`hal_zigbee_send_cmd_to_coordinator()` validates arguments, joined state and
payload length, and returns the actual immediate result from the platform send
primitive. The Telink implementation must not return `HAL_ZIGBEE_OK`
unconditionally after `zcl_sendCmd()`.

`HAL_ZIGBEE_OK` means accepted for stack processing, not acknowledged by the
coordinator. Documentation and counter names use `submitted`, never `delivered`.
If an APS/ZCL completion callback is later exposed safely, it can replace the
pacing-window ownership rule in a separate decision without changing payload or
queue order.

## Network transitions

- Leaving joined state stops the pump but does not synchronously discard queued
  entries.
- A genuine joined transition schedules the pump.
- TTL prevents stale actions from replaying after a long outage.
- Sequence numbers are assigned before enqueue and continue across all drop,
  expiry and failure outcomes, so the receiver can detect gaps.

## Diagnostics

| Counter | Meaning |
| --- | --- |
| `zb_button_events_dropped` | Queue-capacity eviction |
| `zb_button_events_expired` | TTL removal before successful submission |
| `zb_button_events_send_failed` | Immediate platform submission rejection; counts attempts |
| `zb_button_events_high_water` | Maximum queue occupancy since boot |
| `zb_button_events_submitted` | Entries accepted by the platform send primitive |

The stub gains deterministic failure injection: reject the next `N` coordinator
sends or reject sends until virtual time `T`.

## Acceptance for issue 438

On the BSEED 4-gang, capture physical transitions and coordinator-received
`ButtonEvent` commands for:

- 100 isolated fast taps on each gang;
- overlapping taps on two and four gangs;
- ten bursts each of 2, 3, 5 and 10 clicks;
- a forced 1-second send-rejection interval followed by recovery.

For a clean-network run, received sequences equal emitted sequences and all loss
and send-failure counters remain zero. For injected failures, every missing
sequence is explained by exactly one capacity drop or expiry; retries themselves
do not allocate a new sequence number.

Reference: [upstream issue 438](https://github.com/romasku/tuya-zigbee-switch/issues/438).

