# 04 — Stage 2: cover controller and position

This packet implements phases 3 and 4 after Stage 1 is green. It replaces cover
domain behavior in `cover_cluster.c`; it does not add a parallel position path.

## Hardware evidence boundary

The exact BSEED unit is publicly reported as `TS130F`,
`_TZ3000_vsj5o7dm`, app version 70 and hardware version 1. Its reported Window
Covering state includes current position, moving state, calibration, motor
reversal and calibration time. Attribute `0xf003` is observed as calibration
time. This validates required external behavior, not pins or MCU routing.

Sources: [exact-device Zigbee report](https://github.com/Koenkk/zigbee2mqtt/issues/31940)
and [calibration-time field report](https://community.home-assistant.io/t/endposition-of-roller-shutter-switch/787336).

No public GPIO trace was found. Girier and LoraTap TS130F pinouts are examples of
topology only and must not seed the BSEED profile. Stage 2 software uses stub
composition; hardware testing waits for disconnected-board continuity tracing.

Phase 6 Diagnostics supports isolated input observation and bounded output
pulses during that characterization, with a management-only configuration and
fixed descriptors. Once a cover is configured, both motor pins are claimed by
their relay drivers and probes must reject them. Do not pulse untraced motor
circuitry with a motor or mains connected.

## Phase 3: extract the controller

### Runtime and public contract

`cover_controller` owns a fixed array of three runtimes:

```c
typedef struct {
    cover_motion_t motion;
    cover_position_confidence_t confidence;
    uint16_t travel_position_bp; /* physical calibrated travel, 0..10000 */
    uint16_t open_position_bp;   /* dead-zone projection, 0..10000 */
    uint16_t target_travel_bp;
    uint32_t movement_started_ms;
    uint16_t movement_started_bp;
    uint8_t pending_direction;
    app_timer_t movement_timer;
    app_timer_t position_timer;
} cover_runtime_t;
```

Use controller-local `cover_result_t`; the cluster performs ZCL translation.
Getters return immutable snapshots for adapters/indicators/tests. State
subscribers use fixed-capacity fan-out and receive state only after it changes.

### Relay sequencing

For OPEN or CLOSE:

1. cancel any previous target/timers;
2. submit OFF for the opposite relay and any pending relay;
3. submit ON for the requested logical direction with source COVER;
4. remain STOPPED while interlock defers activation;
5. start motion timing when the relay observer reports actual ON.

STOP submits OFF for both relays and cancels both possible deferred activations.
Motor reversal changes which configured relay represents logical OPEN/CLOSE; it
never changes position orientation.

Interlock owns the dead-time clock. `cover_controller` must not reproduce the
current `last_switch_time`/200 ms scheduler. If both relay observers ever report
ON, submit OFF for both, cancel movement, set confidence UNKNOWN and emit a
diagnostic fault.

Manual mode uses the immutable profile `manual_max_run_ms`, including after
button or Zigbee commands. Protection BLOCKED leaves motion STOPPED and returns
BLOCKED to the caller.

### Persistence extraction

Allocate a cover owner NVM id. Phase 3 stores motor reversal and a format
version. `cover_cluster` has no NVM include after the extraction.

## Phase 4: add calibrated position

### Position mathematics

Use unsigned time differences. Travel times are limited to 429,496 ms, so the
following basis-point products fit in `uint32_t` on the Telink target:

```text
opening physical delta = elapsed_ms * 10000 / effective_open_time_ms
closing physical delta = elapsed_ms * 10000 / effective_close_time_ms
```

Clamp before narrowing to `uint16_t`. Effective time is its configured value or
the non-zero peer; both zero means manual mode.

Dead zones project physical travel into user position:

```text
usable = 10000 - closed_deadzone_bp - open_deadzone_bp
open_position = clamp(
    (travel_position - closed_deadzone_bp) * 10000 / usable,
    0, 10000)
```

For an intermediate GOTO, invert that formula to obtain the physical target.
GOTO 0 and 10000 always target the physical endpoint and enable overrun. Update
position at a fixed 100 ms presentation interval and at every start/stop/target
deadline; movement completion uses an exact one-shot timer, not accumulated tick
rounding.

OPEN/CLOSE endpoint motion clamps the public value on entry into the endpoint
dead zone, continues through calibrated travel, then overruns for `overrun_ms`.
Completion sets the physical endpoint exactly and confidence KNOWN. Intermediate
stops are ESTIMATED. Manual motion and boot after interrupted movement are
UNKNOWN. From UNKNOWN, OPEN/CLOSE run full travel plus overrun, STOP keeps
UNKNOWN, and intermediate GOTO is refused.

### NVM write order

Before energizing a motor, attempt to persist `{motion_active = 1}` with the last
stable position. A failed write records an NVM diagnostic and forces in-memory
position confidence to UNKNOWN, but does not remove manual OPEN/CLOSE/STOP;
motor interlock and the safety timeout remain available without NVM. On stop,
persist times, dead zones, overrun, reversal, physical position, confidence and
`motion_active = 0`. Cover actions are infrequent enough for these state writes;
periodic 100 ms updates never write NVM.

### Window Covering adapter

Keep standard command ids and add GoToLiftPercentage `0x05`. Validate payload
length before reading its byte. Preserve:

| Attribute | Type | Backend |
| --- | --- | --- |
| CurrentPositionLiftPercentage `0x0008` | `uint8` read-only | `100 - ((open_bp + 50) / 100)` |
| Moving `0xff00` | `enum8` read-only | controller motion |
| MotorReversal `0xff01` | boolean writable | controller setter |
| OpenTime `0xff02` | `uint32` ms writable | controller config |
| CloseTime `0xff03` | `uint32` ms writable | controller config |
| ClosedDeadzone `0xff04` | `uint16` basis points writable | controller config |
| OpenDeadzone `0xff05` | `uint16` basis points writable | controller config |
| Overrun `0xff06` | `uint16` ms writable | controller config |

Zigbee2MQTT's `windowCovering` extend shows the standard ZCL value as
0 closed / 100 open only without `coverInverted`; the template must not set
it. Unknown position reads `0xFF`, which Zigbee2MQTT ignores. Update Telink/Silabs tables and source templates in the same
phase; commit regenerated artifacts separately.

## Required tests

Native fake-relay tests:

- open/close/stop, duplicate command, same-direction STOP policy in mappings;
- reversal gap, replacement during gap, STOP during gap and inhibit rejection;
- impossible both-ON recovery and manual safety timeout;
- unequal times, one-time fallback, dead-zone forward/inverse tables, partial
  moves, GOTO crossing, endpoint overrun, wraparound and rounding bounds;
- movement marker write failure degrades position but not manual control and
  power-loss marker restore.

Stub/pytest:

- button and Zigbee paths produce identical controller state;
- reports occur at start, bounded movement updates and stop, not every app tick;
- malformed/invalid writes do not move relays or mutate NVM;
- open/close relays are asserted mutually exclusive after every virtual-time
  step;
- reboot while moving yields UNKNOWN but OPEN/CLOSE/STOP remain usable.

## Completion and hand-off

Meter foundation and Phase 6 management work can proceed when `cover_cluster.c`
contains only adapter state/translation and both Phase 3 and Phase 4 software
gates are recorded. Phase 6 converts the cover adapter to explicit access flags,
command context and candidate-write callbacks without moving travel/reversal
settings into global Device Config. Hardware status may remain blocked, but the
exact missing pin evidence must be listed in `STATE.md`.
