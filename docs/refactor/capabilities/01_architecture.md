# 01 — Capability architecture

Implementation order and gates are in [02_phases.md](./02_phases.md). Current
evidence and blockers are tracked in [STATE.md](./STATE.md).

Execution packets for implementation runs:

- [03_stage_1_composition_contracts.md](./03_stage_1_composition_contracts.md)
  — phases 1–2;
- [04_stage_2_cover.md](./04_stage_2_cover.md) — phases 3–4;
- [05_stage_3_metering.md](./05_stage_3_metering.md) — phases 5 and 7;
- [Phase 6 execution packet](./02_phases.md#phase-6--firmware-cleanup-and-gpio-diagnostics)
  — Zigbee contracts, management services, GPIO ownership and discovery;
- [06_stage_4_protection_profiles.md](./06_stage_4_protection_profiles.md) —
  phases 8–9.

## Scope

This refactor keeps one generic firmware and composes BSEED products from bounded,
reusable capabilities. Product categories remain metadata. There is no outlet,
wall-switch or metered-switch controller. A cover has stateful motor policy and
therefore has a `cover_controller`.

The first hardware targets are:

| Product | Zigbee identity | Repository state |
| --- | --- | --- |
| BSEED 4-gang switch | `TS0726`, `_TZ3002_pzao9ls1`, `EC-GL86ZPCS41` | present as `SWITCH_BSEED_TS0726_4GANG` |
| BSEED metered outlet | `TS011F`, `_TZ3210_drzlxjne`, `TS011F_plug_1` | exact fingerprint absent |
| BSEED cover | `TS130F`, `_TZ3000_vsj5o7dm`, `TS130F` | exact fingerprint absent |

The absent fingerprints must be characterized and receive distinct database
profiles and OTA identities. Pinouts, meter ICs, calibration and image types must
not be copied from another device merely because its Tuya model is also `TS011F`
or `TS130F`.

Firmware cleanup and GPIO diagnostics are part of this refactor. Phase 6 is the
next implementation stage while Phase 5 awaits exact outlet hardware evidence.
It provides fixed management clusters and bounded GPIO discovery without
changing device configuration, rebooting or rebuilding descriptors per probe.
Discovery assists pin characterization; it cannot identify a meter IC, prove
counter routing or supply calibration without physical evidence.

## Compatibility policy

This initiative does not retain firmware, NVM, internal C API or generated-output
compatibility across capability-refactor phases. Each phase deletes the behavior
and persisted layout it replaces. A changed owner-record layout receives a fresh
NVM item state; it is not decoded, translated or preserved from an earlier phase.
The current Zigbee interface is the only supported interface.
Basic private management attributes are removed when Device Config and Device
Diagnostics are introduced. There are no Basic aliases, transition flags,
converter fallback reads or parallel persistence owners. OTA image identity and
safe installation remain release gates; preservation of replaced settings is
not a requirement.

## Current implementation and remaining boundaries

The repository already has the input pipeline, timer service, relay driver,
relay controller, interlock and indicator arbitration. This refactor extends
those modules rather than replacing them.

The working tree implements pure parsing/composition, domain relay results with
four observers and source-owned inhibits, typed action sinks, calibrated
`cover_controller`, a counter HAL, fake meter driver and `meter_service`.
Telink/Silabs counters explicitly reject all routes; only stub capture exists.
`feature_wiring` does not construct meters yet. There is no confirmed outlet
driver, measurement adapter or overload controller.

The remaining management boundaries are concrete:

- `basic_cluster.c` owns private config/diagnostic attributes, writes NVM and
  schedules reboot after config writes, without validating the candidate first.
- `app.c` refreshes diagnostics through Basic; Basic directly reads button-event
  adapter counters, network state and HAL counters.
- `general_commands.c` dispatches writes through a cluster-id chain. Callbacks
  receive an attribute id after storage mutation, with no candidate or result.
- `hal/zigbee.h` has two access modes and no manufacturer metadata or command
  direction; Telink marks every attribute reportable and hardcodes registration.
- Silabs runtime metadata and `custom-zcl.xml` disagree even on Basic config
  type/write access. Source integration templates expose management on Basic.
- GPIO has platform-specific pin encodings, no ownership/capability queries and
  one edge sink installed by `button_input`. Output initialization cannot
  express a checked preload/enable/cutoff/Hi-Z sequence.

The domain rules are:

1. Protocol adapters sit at the outside of the domain graph. They depend on
   services/controllers; services/controllers never depend on Zigbee.
2. Internal cover position is `0 = closed`, `10000 = open`. The Window Covering
   adapter converts to the cluster's lift-percentage convention (`0 = open`,
   `100 = closed`). Zigbee2MQTT's `windowCovering` extend converts that
   standard convention to user-facing `0 = closed`, `100 = open` only without
   `coverInverted`, so the template must not set it.
3. Runtime calibration uses hardware-independent gain values. IC-specific pulse,
   register and reference constants remain in the tagged driver configuration;
   they are not generic meter attributes.
4. Protection holds a relay-controller inhibit while tripped. A one-time OFF
   request alone is not protection because another source could turn the relay
   on again.

## Dependency graph

```text
runtime config string                generated device profile (later)
        │                                      │
        └──────────────┬───────────────────────┘
                       ▼
              device_composition_t
                       │
                       ▼
                 feature_wiring
                       │ constructs and connects
       ┌───────────────┼──────────────────────────┐
       ▼               ▼                          ▼
 input pipeline   cover_controller           meter_service
       │               │                          │
       ▼               │                          ▼
 action_mapper ────────┤                overload_controller
       │               │                          │
       └───────────────┴────────────┬─────────────┘
                                    ▼
                             relay_controller
                                    ▼
                               relay_driver
                                    ▼
                              gpio_manager
                                    ▼
                                 HAL GPIO

Zigbee OnOff ───────────────────────────► relay_controller
Zigbee Window Covering ─────────────────► cover_controller
Zigbee Measurement / Metering ──────────► meter_service
Zigbee overload attributes ─────────────► overload_controller
Zigbee Device Config ───────────────────► device_config_service
Zigbee Device Diagnostics ──────────────► diagnostics_service
                         └─────────────► gpio_probe_service → gpio_manager

cover state ───────────────► indicator_feedback ◄──────── relay/network state
button/gesture state ──────► indicator_feedback ─────────► LED primitive
```

Zigbee adapters subscribe to state and translate calls/results. They are not
part of a controller's dependency path.

## Ownership and structural invariants

| Owner | Owns | Must not own |
| --- | --- | --- |
| `config_parser` | token syntax and normalized parsed values | GPIO, timers, NVM state, endpoints, callbacks, Zigbee init |
| `feature_wiring` | construction order, ids, relations and endpoint composition | feature algorithms |
| `device_config_service` | validated global configuration, revision, status, NVM and reboot scheduling | ZCL types, endpoints or feature settings |
| `diagnostics_service` | bounded snapshots from injected diagnostic providers | ZCL framing, feature policy or persistence |
| `gpio_manager` | exclusive pin claims, capability checks and edge routing | relay/cover policy or Zigbee |
| `gpio_probe_service` | one timed observation or pulse, state and safe cleanup | config strings, NVM or descriptors |
| `button_input` / `gesture_fsm` | electrical input and temporal recognition | action meaning |
| `action_mapper` | button/gesture-to-action policy and mapping configuration | GPIO or Zigbee framing |
| `relay_controller` | logical state, startup/inching/interlock policy, inhibit state and relay persistence | button semantics or Zigbee statuses |
| `relay_driver` | relay GPIO/polarity/latching pulses | switching policy |
| `cover_controller` | movement, target, position, reversal and cover persistence | relay GPIO or Zigbee attributes |
| `meter driver` | IC I/O and conversion to normalized samples | accumulated energy/NVM/Zigbee/protection |
| `meter_service` | samples, calibration override, energy and meter NVM | endpoints or relay policy |
| `overload_controller` | threshold state machine and protection NVM | relay GPIO or measurement attributes |
| `indicator_feedback` | indicator mode, arbitration, LED presentation and indicator NVM | product categories |
| Zigbee clusters | command/attribute conversion and reporting | feature state machines or another feature's NVM |

The following are non-negotiable:

- only `relay_driver` writes relay pins;
- composition claims every physical pin before enabling hardware; diagnostics
  cannot borrow feature-owned or platform-reserved pins;
- only `relay_controller` calls `relay_driver`;
- every mains-output change and every protection lock passes through
  `relay_controller`;
- a cover relay pair is one interlock group and is never exposed as independent
  OnOff outputs;
- interlock is the sole enforcement of reversal dead time; the cover controller
  observes when the deferred relay actually energizes and starts timing then;
- GPIO meter pulses never enter `gpio_edge_queue` or `button_input`;
- only `indicator_feedback` drives indicator LEDs;
- loss of Zigbee does not stop buttons, covers, metering or protection;
- optional-capability failure does not disable unrelated capabilities unless an
  immutable profile explicitly selects fail-closed behavior.

## Normalized composition

The runtime string and a future generated profile produce the same bounded
`device_composition_t`. Parsed hardware data is immutable after validation.
Runtime settings live in feature-owned NVM records.

```c
typedef struct {
    relay_driver_config_t relays[10];
    button_config_t       buttons[11];
    indicator_config_t    indicators[5];
    cover_config_t        covers[3];
    meter_config_t        meters[2];
    overload_config_t     protections[2];
    action_mapping_t      mappings[ACTION_MAPPING_MAX];
    endpoint_config_t     endpoints[10];
    uint8_t               relay_count;
    uint8_t               button_count;
    uint8_t               indicator_count;
    uint8_t               cover_count;
    uint8_t               meter_count;
    uint8_t               protection_count;
    uint8_t               mapping_count;
    uint8_t               endpoint_count;
} device_composition_t;
```

The exact bounds remain compile-time constants and are preflighted before any
hardware initialization. Arrays and NVM item ranges use the same constants;
duplicate ids, invalid references, conflicting GPIO roles, overlapping cover
relays, unsupported counter resources and endpoint-capacity overflow reject the
whole composition.

OTA manufacturer id, image type and device role remain build/profile metadata.
They are not writable runtime capability data. A recognized image token is
validated against, and cannot override, the built image identity.

Construction is split into two operations:

```text
config_parser_parse(string, &composition)  # no side effects
feature_wiring_build(&composition)         # construct and connect owners
```

`feature_wiring_build` initializes safe relay outputs first, registers relays and
interlocks, constructs indicators/buttons/covers/meters/protection, restores each
owner's NVM, installs action mappings, composes Zigbee endpoints, applies startup
policy, then enables input/service scheduling. A Zigbee join or reconnect is not
an initialization prerequisite.

## Relay contract needed by capabilities

`relay_controller` returns a domain result rather than
`hal_zigbee_cmd_result_t`:

```c
typedef enum {
    RELAY_RESULT_APPLIED,
    RELAY_RESULT_UNCHANGED,
    RELAY_RESULT_DEFERRED,
    RELAY_RESULT_BLOCKED,
    RELAY_RESULT_INVALID,
} relay_result_t;

typedef enum {
    RELAY_SOURCE_BUTTON,
    RELAY_SOURCE_GESTURE,
    RELAY_SOURCE_ZIGBEE,
    RELAY_SOURCE_TIMER,
    RELAY_SOURCE_INTERLOCK,
    RELAY_SOURCE_STARTUP,
    RELAY_SOURCE_COVER,
    RELAY_SOURCE_PROTECTION,
} relay_request_source_t;
```

State notification becomes fixed-capacity fan-out. Subscribers receive actual
applied transitions, never requested/deferred states. Registration failure is a
composition error. Zigbee maps `relay_result_t` to ZCL status at its boundary.

Protection uses a source-owned inhibit mask. While a relay is inhibited, OFF is
always accepted, ON/PULSE/ON_TIMED are blocked, and releasing the inhibit does
not silently restore a previous ON state.

## Cover controller

```c
typedef struct {
    uint8_t  open_relay_id;
    uint8_t  close_relay_id;
    uint16_t reversal_dead_time_ms;
    uint32_t manual_max_run_ms;
    uint32_t default_open_time_ms;
    uint32_t default_close_time_ms;
    uint16_t default_closed_deadzone_bp;
    uint16_t default_open_deadzone_bp;
    uint16_t default_overrun_ms;
} cover_config_t;

typedef enum { COVER_STOPPED, COVER_OPENING, COVER_CLOSING } cover_motion_t;
typedef enum { COVER_POSITION_UNKNOWN, COVER_POSITION_ESTIMATED,
               COVER_POSITION_KNOWN } cover_position_confidence_t;

cover_result_t cover_open(uint8_t cover_id);
cover_result_t cover_close(uint8_t cover_id);
cover_result_t cover_stop(uint8_t cover_id);
cover_result_t cover_goto(uint8_t cover_id, uint16_t open_position_bp);
```

Position uses basis points (`0..10000`) and unsigned elapsed-time arithmetic.
Travel times are limited to 429,496 ms so all basis-point products fit in the
Telink target's 32-bit arithmetic.
`open_time_ms == 0 && close_time_ms == 0` selects manual mode. If one travel time
is zero, its effective value is the non-zero peer; this permits calibration one
direction at a time. Manual mode supports OPEN/CLOSE/STOP, rejects GOTO and uses
the immutable `manual_max_run_ms` safety stop.

Travel times cover physical endpoint-to-endpoint motion, excluding overrun.
Dead zones are basis points of that calibrated travel; each is `0..9999` and
their sum must be below 10000. They map physical elapsed position into the usable
logical range. Endpoint overrun starts only for OPEN/CLOSE or GOTO 0/10000,
clamps reported position immediately, then stops after `overrun_ms` and marks the
endpoint KNOWN. Intermediate timed stops are ESTIMATED.

While the position is UNKNOWN, an endpoint move is timed as if it started at
the opposite endpoint (full travel plus overrun), and only its completion makes
the position KNOWN. STOP, an external relay change or the manual safety stop
never raise confidence. Intermediate GOTO is refused (`ACTION_DENIED`) while
UNKNOWN. A GOTO in the current direction of motion retimes the running motor
without a stop or NVM write; a GOTO less than 1 ms of travel away is a STOP
(nothing when already stopped).

Every motor start arms its cutoff (movement timer when calibrated, safety timer
in manual mode) with one retry; if neither attempt arms it, the motor is
stopped at once. `cover_controller_state()` returns the last computed state
without side effects; the 100 ms position timer refreshes it while moving.

On movement start, the controller persists a compact `motion_active` marker. On
clean stop it persists position and confidence and clears the marker. Booting
with the marker set yields UNKNOWN position while OPEN/CLOSE/STOP remain usable.
Persistent layouts are versioned. A layout change clears the previous owner
record; it is never reinterpreted at a different size.

The adapter maps:

```text
Up/Open                 → cover_open
Down/Close              → cover_close
Stop                    → cover_stop
GoToLiftPercentage p    → cover_goto((100 - p) * 100)

internal open basis points → CurrentPositionLiftPercentage
                              = 100 - ((open_bp + 50) / 100)
UNKNOWN position or manual mode → CurrentPositionLiftPercentage = 0xFF
```

`0xFF` is the ZCL non-value for `uint8`; Zigbee2MQTT ignores lift values above
100. Setting writes are range-checked in the VALIDATE phase: travel times above
429,496 ms and deadzones that are not below 10000 or whose sum is not below
10000 fail with `INVALID_VALUE` and leave the attribute unchanged. Overrun
accepts the full `uint16` range.

Existing custom `Moving` (`0xff00`) and `MotorReversal` (`0xff01`) semantics are
preserved. New controller-owned settings use `0xff02..0xff06` for open time ms,
close time ms, closed deadzone basis points, open deadzone basis points and
overrun ms respectively. Writes validate and persist through controller APIs.

## Meter driver and counter HAL

`src/hal/gpio_counter.h` is separate from `src/hal/gpio.h` edge capture. Its
contract provides a monotonic 32-bit pulse total plus a capture timestamp and
overflow/error diagnostics. Platform code extends narrower peripheral counters.
Counter resources are reserved during composition; unsupported pin/resource
combinations fail preflight rather than falling back to the button IRQ queue.

Telink TLSR8258 GPIO-trigger timers and available Silabs timer/counter routing
are platform implementations of the same API. The interface does not promise
that every MCU can host two arbitrary simultaneous counters.

The driver interface is static and tagged by IC type:

```c
typedef struct {
    uint32_t voltage_mv;
    uint32_t current_ma;
    int32_t  active_power_mw;
    uint32_t apparent_power_mva;
    int32_t  reactive_power_mvar;
    int16_t  power_factor_milli;  /* -1000..1000 */
    uint64_t energy_delta_uwh;
    uint16_t valid_mask;
} meter_sample_t;

typedef struct {
    uint32_t voltage_gain_ppm; /* 1000000 = 1.0 */
    uint32_t current_gain_ppm;
    uint32_t power_gain_ppm;
} meter_calibration_t;
```

Unsupported quantities have their validity bits clear; they are not fabricated.
HLW/BL pulse frequencies, SEL state, UART status and checksums remain driver
diagnostics. Driver-specific base constants and pins are a tagged union in
`meter_config_t`; normalized per-device gain overrides are owned by
`meter_service`.

`meter_service` schedules sampling with `timer_service`, caches the last valid
sample, accumulates `energy_delta_uwh`, and offers bounded subscribers. It owns
versioned calibration and energy records. Energy checkpoint frequency is a named
policy with a platform-endurance test and a documented maximum power-loss window;
no implementation may write on every pulse or every sample.

## Overload controller

Each instance binds one meter to a relay mask. Runtime thresholds are clamped to
non-zero immutable voltage/current/power ratings from the profile; zero disables
only a user threshold, never a hard limit.

```text
ARMED → TRIP_PENDING → TRIPPED → CLEAR_PENDING → ARMED
```

A configured limit must be exceeded continuously for `trip_delay_ms`. Trip
acquires the protection inhibit, submits OFF for every protected relay, records
the cause and persists state. Hard trips require an explicit clear after all
required readings have remained valid and safe for `reconnect_delay_ms`. User
trips may auto-clear only when explicitly enabled; clearing releases the inhibit
but never turns a relay on. Invalid samples cancel pending user trips. Immutable
profile policy decides whether a prolonged meter failure is fail-open or
fail-closed.

## Zigbee and endpoint composition

Adapters have one backend id and no second state copy:

| Adapter | Backend |
| --- | --- |
| OnOff / startup / inching | `relay_controller` |
| Window Covering | `cover_controller` |
| switch and cover-switch configuration | `action_mapper` |
| Electrical Measurement | `meter_service` sample |
| Metering | `meter_service` accumulated energy |
| overload custom attributes | `overload_controller` |
| indicator attributes | `indicator_feedback` |
| Basic | identity and standard device state only |
| Device Config | `device_config_service` |
| Device Diagnostics | `diagnostics_service` and `gpio_probe_service` |

A standalone meter needs no relay endpoint. Whole-device metering is attached to
the endpoint selected by `endpoint_config_t` (endpoint 1 by default for BSEED),
not inferred by `meter_service`. Standard multiplier/divisor attributes perform
unit conversion; calibration and reset-energy writes call service APIs.

Cluster registration and attribute tables must be added to Telink, Silabs and
stub layers together. Generator templates are edited, then Z2M/ZHA/HOMEd outputs
are regenerated; generated files are never hand-edited.

## Zigbee HAL and protocol ownership

`hal_zigbee_cluster` carries `cluster_id`, `manufacturer_code`, explicit
server/client `role`, attribute storage/count, `cmd_callback` and
`attr_write_callback`. Attribute access is an independent bitmask:
`HAL_ATTR_READ`, `HAL_ATTR_WRITE`, `HAL_ATTR_REPORT`. Reporting is opt-in and
requires readable state. Metadata/type/access errors reject registration before
the SDK receives any partial descriptor graph.

Command context contains endpoint, cluster id, command id,
`manufacturer_specific`, manufacturer code, direction, immutable payload and
payload length. Reads, writes, reports and command dispatch resolve the same
endpoint/cluster/role/manufacturer tuple. A mismatched manufacturer or direction
must not reach a service. Manufacturer code is a named project protocol
constant, selected and checked before the Phase 6.1 gate; neither zero nor a
stock Tuya OTA manufacturer id is an inferred allocation.

Per-cluster writes have distinct validation and commit responsibilities.
Validation of a typed, length-checked candidate is side-effect free and may be
called repeatedly by an SDK. Commit calls the owning service once, persists as
required and updates the mirror only on success. Never persist, schedule work or
change GPIO in a pre-change permission callback. A rejected record leaves its
setting/NVM unchanged; status/error diagnostics may describe the failure.
Ordinary multi-attribute writes return per-record results and permit independent
successes. Writes without response use the same semantics but send no response.
For undivided writes, validate all records then commit atomically, or reject the
command before any mutation; Phase 6 does not require a general transaction
engine. SDK probes and the final external-storage callback must not commit twice.
A rollback after acknowledging success is insufficient. Storage-only attributes
may use the generic validated commit; service-owned settings use their adapter.

Remove the global write dispatcher and registration API. Keep stack-specific
handlers for standard clusters that require them. Telink's private-cluster path
registers arbitrary declared private clusters through generic SDK registration,
preserves raw ZCL metadata and participates in descriptor preflight. Apply it to
existing private clusters as well as `0xFC10`/`0xFC11`. Silabs external storage,
command subscriptions, reporting and generated metadata follow the same contract.
Stub injection must exercise access checks, candidate validation, manufacturer
and direction, rather than modifying attribute buffers before notification.

`src/zigbee/zcl_ids.h` contains cluster IDs only. Cluster-owned `*_defs.h` files
contain attribute IDs, command IDs and wire enums; shared ZCL data types/statuses
live in a separate common header. Delete `consts.h` once consumers are converted.
Use one machine-readable management protocol definition to generate/check C
definitions, Silabs metadata, integration definitions and test constants. Keep
feature protocols in their own definitions; do not expand `device_db.yaml` into
a protocol registry. Every generated surface must match IDs, types, lengths,
access, manufacturer metadata and payload layouts.

## Fixed management clusters and services

EP1 always contains Basic, Device Config (`0xFC10`) and Device Diagnostics
(`0xFC11`), plus applicable OTA/power/poll management clusters. Reserve their
cluster and attribute capacity during composition preflight on all backends.
They exist with no relays/buttons/meters and during invalid-config recovery;
failed feature construction must not prevent configuration repair or enable
partial outputs. Feature endpoint topology is composed normally at boot.
Starting/stopping/reading a probe never alters any descriptor or endpoint id.
Optional attribute/command availability is fixed by the build capability set;
small devices need not include the full diagnostics implementation below.

Boot order is explicit: initialize platform reservations and safe boot GPIO,
prepare identity/config and core diagnostic providers, then read/validate NVM.
Preflight the candidate graph without acquiring pins or conflicting with claims
of the running configuration. Only a valid candidate acquires feature claims and
constructs features. On failure, stop partial work and make outputs safe, retain
any unresolved fault claims, and select management-only recovery. Finalize the
selected descriptor graph, initialize Zigbee once, then start approved features.
Core management does not depend on feature callbacks, LEDs or valid config;
absent diagnostic producers are unavailable, not dereferenced or initialized.
Configuration repair applies through the service and scheduled reboot; it does
not hot-rebuild the graph. Recovery cannot release an uncertain output for probing.

Basic contains only `0x0000..0x0007` (ZCL/Application/Stack/Hardware version,
manufacturer, model, date, power source), `0x0012 DeviceEnabled`,
`0x4000 SWBuildID` and `0xFFFD ClusterRevision`. It does not include config/NVM/reset,
GPIO, button/gesture, network or descriptor aggregation dependencies.
Supply identity/build metadata through the common interface instead of including
platform configuration headers in the cluster.

Device Config uses these attributes; R/W/P mean read/write/report permission:

| ID | Attribute | Type | Access |
| --- | --- | --- | --- |
| `0x0000` | schema_version | uint16 | R |
| `0x0001` | config_revision | uint32 | R |
| `0x0002` | device_config | bounded long character string | R/W |
| `0x0003` | config_status | enum8 | R/P |
| `0x0004` | last_config_error | enum8 | R/P |
| `0x0010` | status_led_mode | enum8 | R/W |
| `0x0011` | multi_press_reset | uint8 | R/W |

`device_config_service_apply(const char *config, size_t len)` copies into a
bounded candidate, rejects embedded NUL/truncation/invalid encoding, parses and
preflights without constructing hardware, then persists config and revision as
one owner transaction. Publish the accepted candidate and increment revision
only after successful persistence; revision overflow is an error. An identical
candidate in READY or REBOOT_PENDING is a no-op. In recovery, a valid identical
candidate may retry application by reboot without rewriting/incrementing revision.
Successful topology changes set REBOOT_PENDING and
schedule one bounded-delay reboot. Reject additional changed config while
pending. Parse/resource/NVM failure records status/error without changing the
accepted config, revision or scheduling reboot. Boot validates the stored record;
READY means it is applied, ERROR means recovery is needed. Status/error are
runtime state, not a reason to write NVM on every failed request.
Service state contains plain text and an explicit length; the adapter owns ZCL
long-string encoding. Define one text-capacity bound including NUL storage and
validate both payload length and encoded buffer capacity before copying. The
service must not inherit the current `config_nv.h` wire-shaped storage contract.

`device_config_service_get_state()` exposes a coherent snapshot, including the
accepted config (pending until reboot). LED mode and reset count use service
setters with validation, persistence and subsystem updates; they do not reboot
or increment the topology config revision. LED modes are NETWORK, OFF and ON,
applied under existing network/reset indication priority; absent dedicated LED
hardware returns unsupported. Reset count uses the reset service's supported
bounds. Move the Basic-owned LED record into service ownership and give changed
layouts explicit clearing steps and fresh IDs where needed. NVM failure must be
observable to callers; the current void config writer is insufficient.

`diagnostics_service` snapshots existing counters through injected provider
callbacks/records. `feature_wiring` connects HAL, input, network, descriptor and
Zigbee-event providers. No domain service includes a Zigbee cluster header.
Counter production stays with the subsystem; the diagnostics cluster only
projects snapshots and schedules bounded reporting.

Device Diagnostics has a mandatory `0x0001 diagnostics_capabilities` bitmap32 R
attribute describing included counter groups, pin-info, input and output probe
support. Bits indicate build/backend support, not whether a particular pin is
free. The counters below are optional by group, uint32 R with opt-in P support
except the enum8 descriptor error. Uptime is modulo-2^32 milliseconds. Read fresh
snapshots; absent groups return unsupported, not fabricated zero measurements.
Other counters retain documented source wrap/saturation semantics.

| Range | Attributes in ascending ID order |
| --- | --- |
| `0x0000` | uptime_ms |
| `0x0010..0x0012` | gpio_edges_captured, gpio_edges_dropped, gpio_rearm_limit_hits |
| `0x0020..0x0021` | button_events_emitted, gestures_emitted |
| `0x0030..0x0034` | zb_button_events_submitted, dropped, expired, send_failed, high_water |
| `0x0040..0x0045` | network_transitions, losses, joins, announce_attempts, failures, steering_attempts |
| `0x0050..0x0051` | descriptor_validation_failures, last_descriptor_error |

Feature settings stay local: relay startup/inching/interlock/indicator on OnOff,
travel/reversal on Cover, meter calibration/energy reset on measurement adapters.

## GPIO ownership and capabilities

`gpio_manager` owns a fixed claim table. Each claim identifies a category and
instance, so two relays cannot share a pin or release each other's claim.
Categories include NONE, PLATFORM, BUTTON, RELAY, COVER, LED, DIAGNOSTICS,
METER and BATTERY; all ADC/PWM/serial/counter pins must participate. Reserve
platform pins first, then preflight and claim the whole composition before GPIO
initialization. Roll back claims and force outputs safe on construction failure.

Cover motor pins belong to the underlying relay-driver instances, annotated with
their cover relationship. They are not claimed twice by relay and cover layers.
COVER is available for any independent cover-owned I/O. Only relay drivers drive
configured relay pins; a diagnostics pulse is limited to unclaimed pins.

APIs include claim/release with owner identity, owner lookup, validity and
probe-allowed checks. HAL capability queries distinguish INPUT, OUTPUT, PULL_UP,
PULL_DOWN, INTERRUPT, PROBE_SAFE and RESERVED, with support for platform-specific
pull modes and autonomous bounded output. Valid MCU pins are not automatically
probe-safe. Platform/package/module reservations deny flash, oscillator, debug,
radio and other dedicated signals. Board restrictions may further narrow this
allowlist; missing capability evidence denies the affected operation.

Probe-safe means permitted by the declared platform/board capability map. It
does not establish the electrical safety of an unknown attached circuit. Output
tests require a characterized test connection at isolated, current-limited low
voltage. Discovery does not require a completed device profile, but it does
require a known MCU/module and its verified reservations.

The manager owns the HAL's single edge sink and routes edges by current pin
claim to bounded button/probe queues. ISR work only enqueues/schedules. Probe
edges never enter gesture/action processing and cannot replace button capture.
Tag queued probe edges with a session generation so released/reclaimed pins
cannot deliver stale events. Meter pulses retain the dedicated counter HAL and
resource preflight; meter pins and counter resources cannot be borrowed by probes.

## Resource limits and small-device behavior

Mandatory work is the common HAL, configuration repair, GPIO ownership and a
small capability-aware Diagnostics shell. Counter groups, pin-info, input probe
and output probe are independent build capabilities with explicit dependencies.
Output requires pin-info and verified cutoff; input requires pin-info and capture.
Defaults include only capabilities whose target resource/behavior gates pass.
Do not infer resources from the number of LEDs, relays or endpoints. Shared
firmware may select capabilities by MCU/build limits without requiring a rebuild
for each probed pin or a separate implementation per product.

Disabled facilities allocate no probe queues, cutoff tasks, retry cache, counter
mirrors or reporting slots and register no providers. Keep optional tables in
flash where supported; index claims by actual platform pins, never by all 65536
wire IDs. Use existing counters, fixed arrays and one active probe; no trace
history, periodic pin scanning, generic debug console or general plugin registry.
An exhausted optional resource returns unsupported/busy and leaves local control,
meter resources, configuration and OTA usable; it must not reject a valid product
composition merely to reserve diagnostics capacity.

Set explicit per-target flash/OTA-slot, static RAM, worst-case stack, descriptor,
timer/IRQ and reporting budgets from measured baselines in Phase 6.1. Record
deltas and remaining headroom at every substage; fitting a linker region alone
is insufficient. Never increase shared global capacities to hide diagnostics
growth. If core management cannot fit, optimize/reduce optional scope and leave
that target's gate open; do not silently drop a supported small device.

With reporting unconfigured and no active probe, diagnostics add no periodic
timer, scan, report traffic or sleep inhibition. Counters are read on demand;
reporting requires explicit configuration and a
documented rate bound. Active probes may temporarily restrict sleep, then restore
normal end-device polling/sleep on every exit. Z2M exposes normal product controls
by default, with supported diagnostics opt-in; no per-pin entities, per-counter
entity explosion or controls for absent hardware. Management clusters stay fixed;
UI and optional attributes reflect build capabilities, not every possible module.

## GPIO probe service and wire contract

`gpio_probe_service` has at most one active INPUT or OUTPUT_PULSE session, with
IDLE and latched FAULT states outside normal activity. It supports
start-input(pin, pull, edge-mode, timeout), stop, pulse(pin, active-level,
duration), read-pin and reset-probe-counters, plus immutable state snapshots.
Domain APIs use `hal_gpio_pin_t`; wire pin IDs are portable uint16 values
`(port_index << 8) | pin_index`, A=0, with `0xFFFF` invalid. HAL translates these
to native encodings after strict validation. Never transmit or narrow Telink
bitmask pins or the stub's packed pin values directly. Z2M presents names such
as B5/C3; golden tests cover round trips on all platforms.

GPIO-access operations reject reserved/non-probeable pins, unsupported capabilities and
claims by other owners. A start/pulse while busy is rejected without extending
or replacing the session; STOP is idempotent. READ_PIN reads the current input
session or takes a short-lived input claim while idle, then restores Hi-Z. It
cannot reconfigure an output pulse or peek through another owner's claim.
Counter reset is idle-only and affects probe counters, never global diagnostics.

Input timeout is 1..300 seconds. Pull and rising/falling/both-edge selection are
validated before claim/configure/watch. Snapshot initial level without counting
an edge; count matching captured transitions with saturation, store raw last-edge
millis, and expose queue drops as an error. Unsupported IRQ routes or queue
overflow stop the probe safely. Global GPIO capture totals may include probe
edges; button/gesture counters must not. Reporting is coalesced, not per edge.

Output duration is 1..500 ms as a design ceiling, with a UI default of 100 ms
clamped to the advertised per-pin maximum. Phase 6.1 must investigate actual
cutoff routing, timing error, reset/sleep behavior and resource conflicts before
the output backend is committed. Include cutoff tolerance in the physical bound:
a 500 ms request plus unbounded scheduling delay does not meet it. Platforms may
advertise a lower maximum or no output support. This does not block management,
pin-info or proven input support. The pulse path is:

```text
validate → claim → preload inactive → arm verified local cutoff
         → enable output at inactive level → activate
         → inactive → disable/Hi-Z with no pull → release
```

HAL must provide checked output preloading/enabling, Hi-Z restoration and a
bounded cutoff primitive with explicit resource failure. `hal_gpio_init` and
the void `app_timer_start` alone cannot establish these guarantees. A platform
must prove the cutoff bound under scheduler delay, network loss, sleep and
retention, or reject output probing as unsupported. A hardware cutoff may act
autonomously; application cleanup runs in task context. It must not borrow the
watchdog or a meter counter, and a watchdog reset alone is not a 500 ms cutoff.
Inhibit incompatible sleep while a probe is active. Reset/retention must not
restore an active output from cached GPIO configuration.

All completion/failure/STOP/reset/reboot paths converge on idempotent
`gpio_probe_force_safe()`: cancel capture and timers, drive an owned pulse
inactive, disable output/pulls, invalidate queued callbacks and release only its
own claim. Reject before enabling if cutoff setup fails. If hardware cleanup
cannot be confirmed, latch a fault, keep the claim and deny further probes;
never advertise IDLE/released while an output might remain driven. No probe
state is persisted or resumed after boot. Loss of Zigbee cannot delay cutoff.

When a probe capability is included, these state attributes are R with opt-in P:

| ID | Attribute | Type |
| --- | --- | --- |
| `0x0100` | probe_state | enum8 (IDLE, INPUT, OUTPUT_PULSE, FAULT) |
| `0x0101` | probe_pin | uint16 portable pin ID |
| `0x0102` | probe_level | uint8 (0, 1, 0xFF unknown) |
| `0x0103` | probe_edge_count | uint32 |
| `0x0104` | probe_last_edge_ms | uint32 |
| `0x0105` | probe_error | enum8 |
| `0x0106` | probe_generation | uint32 |

Accepted start/reset operations advance generation; cleanup invalidates pending
work. Keep the completed pin/counters readable until the next accepted start or
explicit reset. Rejected requests return a result without replacing active state.
Errors distinguish invalid pin/argument, reserved/claimed, unsupported, busy,
resource failure, capture overflow and cleanup failure. Assign enum values in
the shared protocol definition before implementation.

Commands are client-to-server and manufacturer-specific. Fields are explicitly
decoded little-endian with exact length checks; never cast a C struct:

| ID | Command | Payload fields | Bytes |
| --- | --- | --- | --- |
| `0x00` | START_INPUT_PROBE | pin:u16, pull:u8, edge_mode:u8, timeout_s:u16 | 6 |
| `0x01` | STOP_PROBE | none | 0 |
| `0x02` | PULSE_OUTPUT | request_id:u32, pin:u16, active_level:u8, duration_ms:u16 | 9 |
| `0x03` | READ_PIN | pin:u16 | 2 |
| `0x04` | RESET_PROBE_COUNTERS | none | 0 |
| `0x05` | GET_PIN_INFO | pin:u16 | 2 |

GET_PIN_INFO is a read-only metadata query, including for claimed/reserved pins;
it never reads a level, claims/configures GPIO or changes probe state. Its
server-to-client response `0x05` contains pin:u16, valid:u8, capabilities:u16,
owner:u8, owner_instance:u8 (0xFF if absent), max_pulse_ms:u16 (0 if unavailable).
Invalid pins return valid=0 and no capabilities. Query one pin per request;
there is no bulk scan, inventory cache or per-pin entity. Actual operations
recheck ownership/resources; a metadata response never reserves permission.

Return mapped ZCL status for ordinary operations; READ_PIN also refreshes state.
PULSE_OUTPUT replies with server-to-client `0x02`: request_id:u32, result:u8,
generation:u32. Success means accepted, not completed; completion is probe state.
Carry stack-provided sender identity and source endpoint into command context.
Pulse retry identity is sender/endpoint/request_id, independent
of ZCL sequence; compare exact operation arguments before recognizing a repeat.
Reserve one of at most two fixed retry records before accepting output. Retain
it through the pulse and five seconds after completion/stop/failure; return the
stored acceptance result/generation for an identical retry without pulsing or
extending time. Reject changed arguments with the same ID; reject new requests
as busy when all records are protected instead of evicting them. No pulse queue.
This cache exists only in output-enabled builds and does not generalize to all
commands. Clients use fresh IDs and retry within that window; after expiry or
device reboot they must read state and require a fresh explicit user action.
There is no persistent or cross-reboot exactly-once guarantee. Invalid frames
never allocate records; STOP remains available even when the cache is full.

Management cluster topology is constant across runtime feature configurations.
Z2M uses capability metadata and hardware presence to offer config/status/revision
and opt-in diagnostic controls, never normal switch/cover entities for probes.
Do not create LED controls on LED-less products or expose disabled counters.
Build from source templates and shared protocol metadata; update ZHA and HOMEd
definitions too.
Test repeated input/pulse/stop operations against identical descriptor snapshots
and a coordinator session without re-interview or reconfigure. Provision an
unknown board with management-only configuration once; probing itself never
edits that configuration. A subsequent finalized feature config may require
normal coordinator descriptor refresh and is outside the probe lifecycle.

## Repository operating constraints

Before code discovery, use CodeGraph when `.codegraph/` exists. Otherwise use
`rg`/`rg --files`. Size files before reading; read ranges for files over about
800 lines. Never dump `device_db.yaml` or generated converters/quirks/docs. Find
an exact database key or fingerprint with capped `rg -n -m`, then read only that
block. Do not print raw build/test logs into an agent transcript: redirect to a
log, report the status and relevant summary, and show at most the final 80 lines
on failure.

All builds and tests for this refactor run in the existing x86_64 Colima profile:

```text
profile: tuya-telink-build
repo:    /Users/desty/Projects/tuya-zigbee-switch
```

Use `colima ssh --profile tuya-telink-build -- sh -lc '<command>'` and redirect
verbose output inside the profile to `/tmp/capabilities-<stage>.log`. Do not use
the stopped default profile or install replacement toolchains on the host.
