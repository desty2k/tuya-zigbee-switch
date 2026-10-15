# 02 — Implementation phases

The contracts are fixed in [01_architecture.md](./01_architecture.md). Update
[STATE.md](./STATE.md) as each gate is exercised.

The refactor has nine phases. Each phase replaces the behavior it owns in the
same change: no parallel implementations, dead adapters or commented-out paths.
This initiative preserves no firmware or NVM compatibility between phases: an
owner-record layout change clears the previous record instead of translating it.

| Execution stage | Phases | Implementation packet |
| --- | --- | --- |
| 1 | 1–2 | [composition and contracts](./03_stage_1_composition_contracts.md) |
| 2 | 3–4 | [cover controller and position](./04_stage_2_cover.md) |
| Meter foundation | 5, generic software | [meter service](./05_stage_3_metering.md) |
| Management and discovery (next) | 6.1–6.5 | [Phase 6 packet below](#phase-6--firmware-cleanup-and-gpio-diagnostics) |
| Meter hardware and adapters | remaining 5, then 7 | [meter service and Zigbee adapters](./05_stage_3_metering.md) |
| Protection and release | 8–9 | [protection, profiles and release](./06_stage_4_protection_profiles.md) |

One implementation run may complete both phases in its stage. Commit and record
each phase gate separately so a partially completed run has an unambiguous
restart point.

Phase 6 requires phases 1–4 and can proceed with the fake meter/counter contracts
while Phase 5 hardware evidence is unavailable. Execute 6.1 → 6.2 → 6.3 → 6.4 →
6.5, then use discovery to complete Phase 5. Phase 7 depends on the Phase 6 HAL
and the Phase 5 service; fake-driver adapter tests need not wait for hardware,
but the outlet acceptance gate does. Phases 8 and 9 retain their measurement and
hardware dependencies. A software gate and a hardware gate have separate status.

## Phase 1 — normalized composition root

Scope:

- introduce bounded `device_composition_t` and separate hardware configuration
  from mutable runtime structures;
- make `config_parser_parse()` side-effect free and preserve every accepted
  existing config token and endpoint order;
- move GPIO/component construction, callback registration, endpoint assembly
  and `hal_zigbee_init` to `feature_wiring`;
- preflight counts, references, GPIO-role conflicts, cover relay pairs, endpoint
  capacity and build image identity before touching hardware;
- use the runtime NVM config string as an input; do not change YAML schema.

Tests/gate:

- native parser tests prove no HAL/NVM/Zigbee calls and reject truncated,
  overflowing and cross-referencing-invalid strings;
- stub fixtures compare the complete endpoint/cluster graph and behavior for
  representative switch, relay, cover, relay-less and empty configurations;
- `SWITCH_BSEED_TS0726_4GANG` still preflights as eight endpoints with image type
  `43627` and keeps its existing config string unchanged.

## Phase 2 — capability-safe relay and action contracts

Scope:

- remove `hal/zigbee.h` from `relay_controller`; add `relay_result_t`,
  `RELAY_SOURCE_PROTECTION`, fixed-capacity relay observers and protection
  inhibits;
- notify only actual applied relay transitions, including delayed interlock ON;
- move startup, inching, interlock and relay persistence ownership out of
  `relay_cluster`; keep the cluster as an adapter;
- expand `action_mapper` to own typed relay/cover/system/binding mappings;
  physical switch and cover-switch modules no longer call controllers directly;
- move indicator configuration ownership to `indicator_feedback`; adapters only
  read/write it;
- use versioned relay/mapping/indicator owner records; changed layouts reset to
  safe defaults through explicit clearing steps, with no record translation.

Tests/gate:

- native relay tests cover APPLIED/UNCHANGED/DEFERRED/BLOCKED, observer capacity,
  actual-transition ordering, inhibit behavior and millis wrap;
- action tests cover immediate, click, hold, detached, repeated-direction stop,
  binding and factory reset mappings without Zigbee framing;
- persistence tests restore current owner records and reject/clear incompatible
  layouts without energizing outputs;
- existing switch, relay, cover-switch, interlock, latching and indicator pytest
  suites remain green; searches find no domain header returning a Zigbee type.

## Phase 3 — cover domain extraction

Scope:

- add `cover_controller.*` with OPEN/CLOSE/STOP, motion state, reversal and the
  immutable manual safety timeout;
- configure each cover pair as one interlock group; delete duplicate 200 ms
  safety scheduling from `cover_cluster`;
- base motion state on relay-controller applied-state notifications so the
  interlock gap reports STOPPED;
- move motor reversal and cover NVM into the controller;
- reduce `cover_cluster` to command/attribute conversion and state reporting.

Tests/gate:

- native fake-relay tests cover open/close/stop, duplicate input, stop during a
  deferred reversal, reversal replacement, both-relays-off recovery, protection
  inhibit and safety timeout;
- assertions at every virtual-time step prove both direction relays are never ON;
- existing `test_cover_cluster.py` and `test_cover_switch_cluster.py` behavior is
  preserved, with attribute reports following applied controller state.

## Phase 4 — cover position and calibration

Scope:

- implement 0..10000 open-position tracking, target position, separate effective
  travel times, dead-zone mapping, endpoint overrun and confidence;
- reuse validated position algorithms from the cover-position work, but place
  all timers, calculations and persistence in `cover_controller`;
- implement manual-mode GOTO rejection and calibrated `GoToLiftPercentage`;
- persist motion marker/config/position in a versioned owner record and clear
  the replaced Phase 3 cover record;
- add custom attributes `0xff02..0xff06`, update both platform registrations and
  generator templates, and regenerate integration outputs separately.

Tests/gate:

- table-driven unit tests cover both directions, unequal times, partial moves,
  pause/resume, GOTO crossing, exact endpoints, rounding, dead-zone bounds,
  overrun, millis wrap and power loss while moving;
- stub tests use virtual time only and verify command payload validation,
  reporting and reboot restore;
- Z2M and ZHA expose user-facing `0 = closed`, `100 = open`; existing Moving and
  MotorReversal ids/types remain unchanged.

## Phase 5 — meter HAL, driver and service

The BL0937 driver, Telink counter routes, meter construction in
`feature_wiring` and the checkpoint policy are implemented for the probe-confirmed
`_TZ3210_drzlxjne` pins (`STATE.md`, Metering design). Remaining work is
reference-meter calibration, hardware counting evidence and the measured
power-cut loss; Silabs has no counter backend.

Scope:

- add `hal/gpio_counter.h` plus stub, Telink and Silabs implementations with
  cumulative count, timestamps, overflow/error diagnostics and resource
  preflight; never route pulses through the button edge queue;
- add `energy_meter.h`, a fake driver and only the real driver confirmed by
  inspection of `_TZ3210_drzlxjne`; reuse validated conversion math from the
  power-monitoring work without importing cluster/persistence coupling;
- add `meter_service.*` with normalized fixed-point units, validity mask,
  bounded subscribers, gain calibration, energy accumulation and versioned NVM;
- define and endurance-test a bounded energy checkpoint policy; sampling and
  energy continue without Zigbee.

Tests/gate:

- native tests cover counter wrap/overflow, resource exhaustion, low/high pulse
  rates, SEL settling where applicable, invalid samples, calibration priority,
  energy remainder math, reboot restore and NVM failures;
- stub tests prove injected meter pulses never change GPIO edge/button counters
  and that relay/button behavior survives meter failure;
- target platform builds prove the confirmed pins can be routed to the selected
  counter resources. An unsupported platform/profile fails preflight clearly.

## Phase 6 — firmware cleanup and GPIO diagnostics

This is the execution packet for management and discovery. The detailed
[HAL, service and GPIO contracts](./01_architecture.md#zigbee-hal-and-protocol-ownership)
define the target. Keep firmware, platform definitions and integration templates
on that single protocol; delete replaced paths in the same substage. No Basic
aliases, compatibility flags, fallback converters or NVM translators are added.

Follow the [small-device resource contract](./01_architecture.md#resource-limits-and-small-device-behavior).
The mandatory management core must fit supported targets. Counter groups and
GPIO facilities are optional build capabilities; missing diagnostics must not
remove product functionality. UI exposes only supported, opt-in diagnostics.
Do not add unrelated debugging facilities or a general diagnostics framework.

### 6.1 — Zigbee HAL contracts and constants

Scope:

- establish baseline and allowed deltas/headroom for flash/OTA image, RAM,
  worst-case stack, descriptor/reporting slots and timer/IRQ resources. Cover the
  lowest-resource supported tier of each MCU family, a single-relay/LED
  composition, an end device and the existing larger target. Select actual
  supported boards from the database; LED count is not a resource metric;
- investigate bounded output cutoff feasibility on each intended backend now,
  including per-pin routing, tolerance, sleep/reset and conflicts with normal
  features. Record proven, pending or unsupported; do not block the management
  and input work merely because output support is unavailable;
- replace READONLY/WRITABLE with independent READ/WRITE/REPORT flags; assign
  access intentionally to every attribute and reject unsupported reporting;
- add cluster role/manufacturer metadata and immutable command context with
  direction, manufacturer header, raw payload and length;
- separate repeatable side-effect-free validation from once-only service/storage
  commit before acknowledgment in Telink, Silabs and stub; convert every adapter and
  remove `general_commands.*` plus the global attribute callback;
- make lookup, external storage, command subscription and reporting respect
  endpoint/cluster/role/manufacturer, including wrong-manufacturer rejection;
- retain required SDK standard handlers and implement generic Telink private
  cluster registration and raw command routing, including existing private
  clusters; remove their cluster-id-specific wrappers when replaced;
- split `consts.h` into cluster IDs, shared ZCL types/statuses and cluster-owned
  definitions; select a named manufacturer code and fix its wire semantics;
- define one management protocol metadata source and generators/parity checks
  for C, Silabs `custom-zcl.xml`, integration templates and test constants.

Tests/gate:

- shared backend contract fixtures prove R, R/W and R/REPORT differ for read,
  write and reporting requests; rejected writes have no state/NVM side effects;
- payload/length, direction, role, manufacturer, endpoint and missing cluster
  errors return the same status on all backends;
- repeated validation/SDK permission hooks cause no persistence or subsystem
  changes; final commit occurs once. Ordinary multi-record writes permit
  per-record success, no-response writes emit no reply, and undivided writes
  either commit atomically or reject before all mutation;
- register two arbitrary test private clusters without adding a switch case;
  verify normal feature commands and SDK standard handlers still work;
- run existing relay/cover/button/poll regressions; build both platform adapters
  or record the exact unverified backend and leave its gate open;
- generated management metadata agrees on ids, types, lengths, permissions and
  manufacturer code; the metadata source's path is recorded in `STATE.md`.
- record initial resource budgets and output feasibility in `STATE.md`; include
  a documented capability-off build so subsequent agents can measure true costs.

### 6.2 — management services and fixed clusters

Scope:

- add `src/device_config/device_config_service.*` with bounded candidate parse,
  side-effect-free composition preflight, NVM result propagation, atomic
  config/revision ownership, status/error and reboot scheduling;
- move global LED/reset settings behind service setters; use fresh owner IDs
  for changed meanings, bump NVM version and clear replaced records. Remove
  conversion-only paths, including `capability_state_migrate_released`, and
  replace their fixtures with reset/default assertions;
- add `src/base_components/diagnostics_service.*` with injected providers; wire
  them in the composition root and update `app.c` to refresh the service;
- add `device_config_cluster.*` and `device_diagnostics_cluster.*` on EP1;
  remove all Basic `0xFF00..0xFF23` management/diagnostic extensions and their
  NVM, reboot and subsystem dependencies;
- reserve fixed management cluster/attribute space in composition, Telink,
  Silabs and stub limits. Keep management reachable in empty and invalid-config
  recovery without initializing partial feature hardware;
- implement the architecture's boot sequence: prepare core management/providers,
  validate candidate, construct features or roll back safely to recovery, then
  initialize one final Zigbee graph and start only approved features. Recovery
  cannot depend on a configured LED or initialized feature callback;
- add the small diagnostics-capabilities attribute and compile-time optional
  counter groups. Disabled producers/tables/reporting state are absent; idle
  diagnostics introduce no polling tasks or reports;
- update integration templates and protocol tests to the management clusters
  in this substage; regenerate outputs in their own commit. Phase 6.5 adds the
  complete probe controls and end-to-end coordinator evidence.

Tests/gate:

- config syntax/length/embedded-NUL/resource rejection leaves accepted config,
  revision and hardware unchanged; NVM failure never schedules reboot;
- successful apply persists one coherent candidate/revision, increments once,
  reports REBOOT_PENDING and schedules one reboot; identical/repeated writes,
  pending writes, revision overflow and interrupted persistence are covered;
- LED/reset settings validate and persist without topology revision or reboot;
  absent LED support is explicit, and reset bounds match the reset subsystem;
- diagnostics work without Zigbee, retain each producer's counter meaning and
  project fresh bounded snapshots without report storms or per-event NVM writes;
- Basic exposes only the standard list. EP1 management works in empty, invalid,
  maximum-capacity, router and end-device compositions; TS0726 remains eight
  endpoints with its feature mapping intact;
- no source/template/test references address removed private Basic management
  attributes; feature-specific private attributes stay on their feature cluster.
- corrupted/truncated NVM, parse failure, claim/construction failure and missing
  producers still permit configuration repair. No partial outputs start and
  Zigbee initializes once; uncertain output claims remain unavailable to probes;
- size/stack/resource deltas stay within 6.1 budgets; the small-switch and
  end-device fixtures retain local control, normal sleep/polling and OTA headroom.

### 6.3 — GPIO ownership, capabilities and event routing

Scope:

- add `src/base_components/gpio_manager.*` with bounded owner+instance claims,
  platform reservations, owner queries and strict native/wire pin conversion;
- claim all button, relay/latching, LED/PWM, battery/ADC and meter/counter/serial
  pins through composition; annotate cover relay ownership without double claim;
- separate GPIO validity from input/output/pull/IRQ/probe-safe capabilities.
  Document module/package reservations, permit board restrictions to narrow the
  map, and deny operations without a proven capability;
- make the manager own the single HAL edge sink and route by pin owner to
  separate bounded input/probe queues. Keep counter HAL independent;
- add checked preload/enable/Hi-Z and cutoff capability contracts needed by
  output probes; audit Telink retention replay and Silabs forced-low output init.
- implement side-effect-free per-pin metadata for GET_PIN_INFO; use compact
  platform pin tables and current claims, without scanning levels or allocating
  a wire-ID-sized table. Stub supports capability-off and restricted-pin fixtures.

Tests/gate:

- same-category/different-instance conflicts, wrong-owner release, reservations,
  invalid pins, resource exhaustion and atomic composition rollback;
- portable pin ID round trips on stub/Telink/Silabs; no uint8 truncation or
  native bitmask encodings on the wire;
- buttons still receive edges during probing; probe edges never trigger actions,
  reset gestures or alter button/drop counters; stale-generation edges are ignored;
- meter pins cannot be claimed by diagnostics, and no probe steals a counter,
  ADC/PWM/serial resource or the watchdog;
- hardware writes only occur after successful claim; failed construction leaves
  outputs safe. Explicit platform allowlist evidence is required before enabling
  that platform's probes, even if the host fixtures pass.
- pin-info returns validity, capabilities, owner/instance and pulse bound for
  free, claimed, reserved and invalid pins without GPIO access/state changes;
- input-only and no-probe builds have no output timers/retry state; diagnostics
  resource exhaustion never steals feature resources or breaks valid composition.

### 6.4 — bounded probe service and diagnostics commands

Scope:

- add `src/base_components/gpio_probe_service.*` independent of ZCL/NVM;
  implement one input session (1..300 s) or output pulse (1..500 ms), read, stop
  and idle-only counter reset, with generation/counters/error snapshots;
- route the architecture's optional `0x0100..0x0106` attributes and supported
  `0x00..0x05` commands through Diagnostics; validate exact payload lengths and
  portable pin IDs. Add bounded GET_PIN_INFO and pulse request/result responses;
- provide local timeout, checked cutoff resource setup, inactive preload,
  safe output enable, inactive restoration, Hi-Z and owner release;
- converge timeout, stop, failed setup, capture overflow, reset and reboot on
  `gpio_probe_force_safe`; retain a fault claim if cleanup cannot be confirmed;
- implement application request IDs for pulses with at most two protected retry
  records, retained during execution and five seconds after termination. Identical
  retries return the stored acceptance result/generation; changed arguments and
  full-cache requests are rejected. STOP never needs a free cache slot;
- bound report frequency, allocate retry/cutoff/capture state only for enabled
  features and preserve local cutoff through Zigbee outage;
- prevent sleep/retention from extending or resurrecting a probe. A platform
  lacking a verified pulse cutoff rejects output probing; input may still work.

Tests/gate:

- service tests without Zigbee cover both pulse polarities, minimum/maximum/
  zero/overflow durations, pulls/edge modes, initial level, saturation, timeout,
  wraparound, busy rejection, repeated stop, read and counter-reset semantics;
- fault injection at claim, preload, timer/cutoff allocation, enable, capture
  and cleanup proves the safe path and no premature release;
- delayed callbacks, queue overflow, repeated/stale frames, network outage,
  pending device-config reboot, reset and retention cannot extend active output;
- application retries with different ZCL sequences cannot repeat an accepted
  pulse within the retry window; test sender isolation, mismatched arguments,
  cache exhaustion, expiry and client behavior after reboot/uncertain timeout;
- timing tests account for cutoff tolerance and each advertised per-pin maximum.
  Unsupported output leaves pin-info/input usable; no-probe builds reject probe
  commands without extra timer/queue/cache allocations;
- stub REPL/pytest exposes capabilities, ownership, command results and virtual
  time. Assert no NVM/config writes, reboot or descriptor changes across probes;
- scope captures on each supported platform prove inactive-before-enable,
  maximum pulse width under load, Hi-Z cleanup and no wake/reset output replay.
  Host timers alone cannot close the physical cutoff gate.
- all exits restore end-device sleep eligibility and reporting/task inactivity;
  verify feature responsiveness and per-target headroom under active diagnostics.

### 6.5 — integration and board-discovery acceptance

Scope:

- keep management clusters fixed, but gate diagnostic exposes and named-pin
  input/pulse/stop controls by capabilities. Diagnostics are opt-in, with no
  per-pin entities or controls for absent LEDs/meters; preserve the simple normal
  product UI. Use GET_PIN_INFO on selection, not periodic scans;
- have the converter preserve pulse request IDs on bounded retries and consume
  their results; after the retry window/reboot, read state and require a fresh
  user action rather than automatically resending an uncertain pulse;
- align ZHA/HOMEd and generated definitions with the same metadata; remove all
  fallback access to Basic private management attributes;
- test a management-only initial configuration on a known MCU/module without
  assigning candidate sibling pins to active features;
- update existing user/contributor docs with command bounds, reserved/claimed
  errors, setup, cleanup and isolated-board characterization instructions;
- characterize B5/B4/C3/C4-style candidates sequentially without config writes,
  reboot, firmware rebuild, endpoint changes or coordinator re-interview.

Tests/gate:

- generator fixtures compare fixed management topology and capability-appropriate
  exposes for empty, single-switch/LED, LED-less, cover, meter-only and mixed
  compositions; verify payloads/manufacturer and retry IDs/results;
- compare full descriptor snapshots before/during/after input/pulse/stop and
  exercise one coordinator session without reconfigure/re-interview;
- record exact board revision, pin role/polarity observations and scope/log
  references. A bounded output on an unknown connected circuit is not made safe
  merely by firmware ownership; follow the isolated low-voltage procedure;
- run the common verification suite and platform builds with resource totals;
  regeneration is a separate commit and metadata generation is idempotent;
- mark software, platform input, platform output and coordinator gates
  separately. Phase 5 still requires meter IC, real routes and calibration.
- close a small-target regression matrix: core-only, pin-info/input-only and
  output-enabled where supported. Compare flash/OTA, RAM/stack, descriptor/timer/
  reporting use, local control and end-device idle behavior to 6.1 baselines.
  No increase of global capacities or loss of a supported target substitutes
  for fitting the budget; leave unmet gates open with the measured deficit.

## Phase 7 — Zigbee measurement adapters

Scope:

- add Electrical Measurement and Metering adapters backed by one meter id;
- expose only valid quantities, with standard multipliers/divisors and saturated
  wire conversions; no adapter samples hardware or owns energy/calibration;
- translate calibration and reset-energy writes to `meter_service` and report
  service notifications on the profile-selected endpoint;
- register clusters on Telink, Silabs and stub; update templates and regenerate
  Z2M/ZHA/HOMEd artifacts in a separate generated-output commit.
- use Phase 6 access flags, command context and per-cluster writes; place ids
  and enums in measurement-owned definitions. Global diagnostics stay in Device
  Diagnostics; measurement calibration and energy reset stay feature-specific.

Tests/gate:

- unit tests cover every conversion boundary, invalid/unsupported attributes,
  malformed commands and signed power-factor/reactive values;
- stub tests cover meter sample to reports, energy across Zigbee outage, reset,
  reboot and a meter-only composition with no relay/button;
- coordinator capture on the BSEED outlet agrees with a reference meter within
  the tolerance recorded in `STATE.md`.

## Phase 8 — overload and indicator relations

Scope:

- add `overload_controller.*` with relay masks, hard/user thresholds, continuous
  trip delay, persistent cause/state, safe-clear delay and optional user-fault
  auto-clear;
- acquire a relay inhibit before submitting protection OFF requests; clamp user
  thresholds to immutable ratings and never auto-restore ON state;
- expose overload settings/state through an adapter and add meter/cover-derived
  base modes to `indicator_feedback` without direct LED writes;
- close persistence ownership: capability state must not remain in Zigbee
  cluster structs or cluster-owned NVM records.

Tests/gate:

- native state-machine tests cover spikes, sustained faults, hysteresis, invalid
  data, multi-relay masks, hard/user limits, clear timing, reboot and meter loss;
- stub tests prove every command source is blocked while tripped, OFF remains
  accepted, release does not re-energize, and unrelated relays still operate;
- indicator priority tests cover relay/cover/meter/error bases under network,
  commissioning and transient feedback overlays.

## Phase 9 — BSEED profiles and release gate

Scope:

- characterize and add exact profiles for `_TZ3210_drzlxjne` and
  `_TZ3000_vsj5o7dm`; preserve the existing TS0726 identity, assign unique image
  types to the two new profiles, then lock all three identities in validator
  fixtures;
- introduce profile/YAML capability fields only after actual pins, meter IC,
  ratings and calibration are known; generation feeds `device_composition_t`;
- validate non-BSEED profiles against the current schema and regenerate all
  derived artifacts from templates/data; exclude unsupported profiles explicitly;
- complete target builds, OTA/NVM migrations and the hardware matrix below.

Hardware gate:

| Device | Required evidence |
| --- | --- |
| TS0726 4-gang switch | 100 taps/gang, simultaneous gangs, detached/binding, relay/indicator consistency, reboot and OTA regression |
| TS011F metered outlet | pin/IC evidence, no-load and calibrated-load readings, energy run, Zigbee outage, overload trip/clear, relay operation after meter failure, OTA |
| TS130F cover | pin/relay evidence, 100 reversals, STOP during dead time, manual timeout, unequal-time calibration, GOTO/dead zones/overrun, breaker loss while moving, OTA |

Use Phase 6 discovery for pin confirmation; a real profile is installed only
after its exact hardware evidence exists. Finalized feature configuration may
change topology at reboot; probe sessions themselves never do.

No phase is release-complete while its applicable hardware gate is unrecorded.
Silabs paths not exercised on hardware are explicitly reported as unverified.

## Verification discipline for every phase

Run inside the `tuya-telink-build` Colima profile and capture verbose output to
`/tmp/capabilities-<phase>-<target>.log`:

```text
make stub/build
make -B unit_tests
make tests
make format
BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build
```

Add the outlet and cover `BOARD=... make board/build` commands after their exact
database keys exist. `make format` must be idempotent and `git diff --check` must
pass. A phase that touches Telink or Silabs HAL/ZCL must build that platform.

Report only exit status, test summary, warnings and relevant resource totals.
On failure, inspect the saved log with a capped search such as
`rg -n -m 40 'error:|warning:|FAILED|Assertion'`; show no more than the final 80
lines when context is still needed. Do not paste complete compiler, pytest, UART
or packet-capture output into chat or commit it under `docs/`.

For repository discovery, obey the size limits in `AGENTS.md`: use CodeGraph
first when indexed, otherwise `rg`; locate an exact `device_db.yaml` key and read
only its block; inspect templates instead of generated Z2M/ZHA/HOMEd files.
