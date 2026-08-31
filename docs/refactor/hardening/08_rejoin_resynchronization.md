# 08 — Rejoin state resynchronization

Phase 2, priority 2.

## Trigger

The network monitor owns the previous observed status. A recovery event is only:

```text
previous status is NOT_JOINED or JOINING
current status is JOINED
and a non-joined status was observed after application initialization
```

Callback registration while the device is already joined does not qualify.
Repeated `JOINED` notifications are deduplicated. Factory-new joining may send an
announce but does not replay button actions.

## Resync plan

State, not history, is resynchronized. A bounded plan captures current values at
the start of recovery:

1. physical relay `OnOff` for relay endpoints 1 through 4;
2. relay indicator state when that attribute exists;
3. Button Event `ButtonState` for switch endpoints 1 through 4;
4. compatibility Multistate Input current value;
5. one device announce.

Snapshots are copied into plan entries so a later state change is either sent as
its normal notification or reflected in a later plan entry, never read through a
stale pointer.

## Pacing

```c
#define ZB_RESYNC_STEP_MS        75
#define ZB_ANNOUNCE_RETRY_MS   1000
#define ZB_ANNOUNCE_MAX_RETRY_MS 60000
```

One task callback submits at most one report or announce. Reports retain their
entry on immediate failure and retry with bounded backoff. A state change while
an entry is pending replaces that entry's snapshot with the newest value because
state coalescing is correct here.

The `0xFC02` event pump and resync pump are separate queues but share a platform
TX budget: event notification has priority, followed by one resync step. Neither
pump drains in a loop.

## Announce behavior

Boot join and rejoin use one announce-retry component. Success is recorded only
when the HAL accepts the request. Failure increments `announce_failures` and
retains pending work. Backoff is capped; a later network loss pauses retries.

## Reporting semantics

`hal_zigbee_notify_attribute_changed()` is insufficient as the only resync
primitive because it depends on the current reporting table and can collapse
rapid changes. The resync service uses explicit snapshot report submission where
available. Normal reporting configuration is not rewritten by the device.

## Diagnostics

| Counter | Meaning |
| --- | --- |
| `network_rejoin_count` | genuine recovery transitions |
| `state_resync_started` | plans created |
| `state_resync_submitted` | snapshot reports accepted by stack |
| `state_resync_failed` | immediate report submission failures |
| `announce_attempts` / `announce_failures` | announce request outcomes |

## Acceptance

- Initial already-joined boot does not create a recovery plan.
- `NOT_JOINED → JOINED` creates one plan; duplicate callbacks create none.
- Four relay and four switch states are reported in deterministic endpoint order
  with at least `ZB_RESYNC_STEP_MS` between submissions.
- A relay change during resync produces a final reported value equal to current
  controller state.
- Injected report and announce failures retry without a busy loop.
- No `ButtonEvent` action is synthesized by resync.
- After a 30-minute coordinator outage, the BSEED becomes available and all
  endpoint states converge without physical interaction.

