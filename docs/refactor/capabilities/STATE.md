# State

Tracking board for the BSEED capability architecture.
The target contracts and gates are in [01_architecture.md](./01_architecture.md)
and [02_phases.md](./02_phases.md).

- Branch: `refactor/input-pipeline`
- Design baseline: `505b82c3`
- Created: 2026-09-01
- Reviewed: 2026-09-07
- Current position: **Phase 5 and 7 software for the BL0937 outlet is implemented (reference-meter calibration open); Phase 6.1 and 6.2 are in progress**

## Phases

| # | Phase | Status | Gate |
| --- | --- | --- | --- |
| 1 | Normalized composition root | done | pure parser and normalized composition root verified in `tuya-telink-build` |
| 2 | Capability-safe relay and action contracts | done | domain contracts, owner-record migrations, native contracts, and switch/relay regressions verified in `tuya-telink-build` |
| 3 | Cover domain extraction | done | controller unit tests + cover regressions |
| 4 | Cover position and calibration | done | native/stub tests verified; TS130F hardware remains blocked on pin evidence |
| 5 | Meter HAL, driver and service | in progress | BL0937 driver, Telink timer0/1 counters, wiring and checkpoint policy implemented and stub/native tested; reference-meter calibration and measured power-cut loss open |
| 6 | Firmware cleanup and GPIO diagnostics | in progress | management core and recovery graph implemented; HAL completion, resource/cutoff evidence and coordinator validation remain open |
| 7 | Zigbee measurement adapters | in progress | Electrical Measurement, Metering and Meter Calibration (`0xFC12`) on Telink/stub with conversion and stub tests; coordinator capture against a reference meter open; Silabs has no counter backend |
| 8 | Overload and indicator relations | not started | protection/priority tests + trip hardware test; the meter never switches a relay until then |
| 9 | BSEED profiles and release gate | blocked | exact outlet/cover profiles and all hardware evidence |

Statuses: `not started`, `in progress`, `in review`, `done`, `blocked`.

Phase 6 starts from the implemented composition/input/relay/cover foundation; it
does not wait for the unknown TS011F meter IC. Resume the hardware part of Phase
5 after discovery and physical characterization. Phase 7 uses the Phase 6 HAL;
its fake-driver software tests can proceed before the outlet hardware gate.
The management core is implemented and exercised in the stub. Hardware GPIO
ownership, cutoff evidence and coordinator validation remain separate gates.

## Next execution: Phase 6

Contracts are in [01_architecture.md](./01_architecture.md#zigbee-hal-and-protocol-ownership),
and the execution packet is in [02_phases.md](./02_phases.md#phase-6--firmware-cleanup-and-gpio-diagnostics).

| Substage | Status | Required result |
| --- | --- | --- |
| 6.1 Zigbee HAL and definitions | in progress | access flags, explicit role metadata, immutable command metadata and cluster-owned write callbacks are active; Telink has descriptor-driven private registration and raw-record extraction, while pre-mirror rejection, protocol generation/parity and resource/cutoff evidence remain open |
| 6.2 Services and management clusters | in progress | Basic identity, service-owned config/reset state, fixed EP1 management clusters, recovery graph and core diagnostics are present; LED policy, generated integrations and coordinator validation remain open |
| 6.3 GPIO ownership and capabilities | not started | all feature/platform pins claimed; portable IDs; side-effect-free pin metadata; shared edge routing; checked GPIO lifecycle |
| 6.4 Probe service and protocol | not started | optional bounded input/output; GET_PIN_INFO; pulse request IDs/results and bounded retry state; safe cleanup without topology changes |
| 6.5 Integrations and discovery | not started | capability-gated opt-in diagnostics, simple product UI, small-target resource/idle regressions and coordinator/bench evidence |

Track software validation, Telink input/output, Silabs input/output and
coordinator acceptance separately. A backend without verified cutoff support
must reject output probing. Probe capability does not imply meter counter
support or electrical safety of an unknown attached load.

Before implementing optional diagnostics, record target budgets and measured
baselines in 6.1: flash/OTA-slot use, RAM/stack headroom, descriptor/reporting
slots, timer/IRQ resources and idle sleep/polling behavior. Include the smallest
supported resource tiers, a simple single-switch/LED composition, an end device
and the existing larger target. No numeric budgets or cutoff support are proven
by this planning update. Repeat comparisons at each substage; a target does not
pass merely because the linker succeeds. Disabled facilities allocate no queues,
tasks, retry cache or report state. Basic product control/configuration/OTA must
remain usable when optional diagnostics are unsupported or resource-exhausted.

## Validated repository facts

| Area | Current fact |
| --- | --- |
| Input/output foundation | input pipeline, timer service, relay driver/controller, interlock and indicator feedback exist |
| Composition | `config_parser` produces a pure validated composition; `feature_wiring` is the sole composition root |
| Relay API | protocol-independent `relay_result_t`, four ordered observers per relay, and source-owned inhibit masks; Zigbee maps results at its adapter boundary |
| Local actions | bounded typed mapper records dispatch relay, cover, binding, button-event, and system intents through injected sinks; adapters retain Zigbee frame encoding |
| Persistence | owner-record IDs start at 40; current source still contains version-5 record conversion; Phase 6.2 removes conversion-only paths and uses explicit clearing for changed layouts |
| Cover | `cover_controller` owns calibrated position, target, safety, settings and NVM; adapter exposes GOTO and calibrated attributes |
| Metering | `E` token → `bl0937` driver → `meter_service` (1 s samples) → Electrical Measurement/Metering/Meter Calibration on endpoint n; Telink counts with timer0/1 (meter images only); Silabs rejects counter routes; stub has gated virtual-time pulse generators |
| Meter persistence | NVM version 8, meter-owned item range starting at 90, clearing step and native checkpoint/calibration/overflow tests exist |
| Basic | identity-only standard attributes; management configuration, reset state and diagnostics have dedicated clusters |
| Diagnostics | `diagnostics_service` owns the core snapshot consumed by the Device Diagnostics cluster |
| Zigbee HAL | explicit read/write/report flags and role/manufacturer metadata; `general_commands.c` and the global write callback are removed, with callbacks owned by writable clusters |
| Telink ZCL | private descriptors carry the cluster manufacturer code and descriptor-driven registration admits arbitrary private clusters; command dispatch receives immutable endpoint/sender/direction/manufacturer/payload metadata and write hooks extract raw records, while SDK-level pre-mirror rejection remains open |
| Silabs definitions | runtime config is long-string writable, `custom-zcl.xml` declares short-string non-writable; runtime command subscription/reporting do not carry the target manufacturer contract |
| GPIO | native uint16 pin encodings differ by backend; no owner/probe capability queries; button input installs the one HAL edge sink |
| Pulse lifecycle | GPIO init lacks checked inactive preload/enable/Hi-Z/cutoff; Silabs enables outputs low; Telink caches modes/levels for retention |
| Management topology/integrations | EP1 has Basic, Device Config, Device Diagnostics and OTA in normal and recovery graphs; source integration templates and generated outputs still need the management-protocol conversion |
| Device schema | flat metadata plus `config_str`; no capability/profile objects |
| TS0726 target | key `SWITCH_BSEED_TS0726_4GANG`, TLSR8258 router, image type `43627`, eight endpoints |
| Exact outlet fingerprint | `_TZ3210_drzlxjne`: probe board `OUTLET_BSEED_TS011F_PROBE` and production board `OUTLET_BSEED_TS011F_PM_DRZLXJNE` (`features: [relay, switch, meter]`, `SB4u;RB5;IC4;EBC2C3D2;M;`, image type `47117`, `build: yes`, `in_progress`) |
| Exact cover fingerprint | `_TZ3000_vsj5o7dm` absent from `device_db.yaml` |

## Metering design (BL0937, TS011F drzlxjne)

Decisions of the first meter implementation (2026-10-15):

| Topic | Decision |
| --- | --- |
| Config token | `E<ic><CF><CF1><SEL>[i]`; IC `B` = BL0937; `i` when SEL low selects voltage. CF/CF1/SEL are claimed against buttons, relays, indicators and the battery pin; CF and CF1 take one counter route each; meter n needs endpoint n |
| Counters | Telink: counter slot n = timer n in GPIO trigger mode on GPIO-to-RISC source n, read without stop/reset; source and timer CPU interrupts masked. The probe uses the same sources, so `meter` and `probe` never share an image (`src/features.mk`, `#error` in `feature_wiring.c`, `board_features.py` probe companions) |
| Sampling | `meter_service` polls the driver every 1 s. CF counts continuously; power is the rate over a window closing at ≥ 50 pulses or 10 s (≤ 2 % counting error). CF1: SEL to voltage, discard 1 s, count 1 s; SEL to current, discard 1 s, window as for power. Cycle 4–13 s |
| Conversion | value = pulses × scale ÷ window ms with scale per Hz: 120670 µV/Hz, 9050 µA/Hz, 1063500 µW/Hz; energy = pulses × 1063500 µJ ÷ 3600 with the remainder carried. Counts above 20 pulses/ms are discarded as faults. Service gains (ppm, 0.5–1.5) apply on top. All 64-bit products/quotients use `fixed_math.h` (the TC32 image links without libgcc) |
| Wire units | RMSVoltage 0.1 V, RMSCurrent mA, ActivePower W (0.1 W would end at 3276.7 W, below 3680 W), CurrentSummationDelivered Wh (divisor 1000 → kWh). Unmeasured values stay at the ZCL invalid value |
| Calibration | Meter Calibration `0xFC12` (manufacturer code 0x1A2B): voltage/current/power gain ppm, writable 500000–1500000, persisted in the meter record with the energy; Reset Energy command `0x00`. Integrations show gains in % |
| Energy persistence | Checkpoint only after energy changed, never within 5 min of the last write, then at ≥ 50 Wh or after 1 h. Loss on power cut ≤ max(50 Wh, 5 min × load) = 307 Wh at 3680 W. Worst case 288 writes/day of a ~48-byte NV item ≈ 3.4 app-module sector rolls/day ≈ 6200 erases per sector in 10 years (100000-cycle flash) |
| Reporting | Adapters notify on every change of a projected value; thresholds and intervals are the coordinator's reporting configuration (Zigbee2MQTT `electricityMeter` defaults) |
| Overload | Not implemented: no auto-trip, the meter never switches a relay (Phase 8) |
| Reuse | Telink timer setup, BL0937 math, cluster ids/units adapted from romasku/tuya-zigbee-switch PR #491 (from HobboRobin/tuya-zigbee-switch-with-metering 8b8cc49); its read-and-reset sampling, driver-owned energy, cluster-owned NVM and relay switching were not taken |

## Target hardware characterization

| Device | MCU/module | Relay/button/LED pins | Meter IC/pins | Ratings/calibration | OTA ids | Status |
| --- | --- | --- | --- | --- | --- | --- |
| TS0726 `_TZ3002_pzao9ls1` | ZTU / TLSR8258 | known in existing config | none known | n/a | stock `54179`, firmware `43627` | profile present |
| TS011F `_TZ3210_drzlxjne` | Telink TLSR8258 (OUI), module unknown; cannot be opened | button B4 active low, internal pull-up (10 presses = 10 edges); relay B5 active high (pull-up alone closes it, 50 ms drive clicks); relay indicator LED C4 active high (lit by C4 pulses, follows the relay as `IC4`); stock firmware switches it independently of the relay | BL0937: CF on C2, CF1 on C3, SEL on D2 (low = current, high = voltage; external pull-down holds it low); 5 W LED bulb: C2 6.4 Hz, C3 3.5 Hz (current), both silent with the relay off; D2 high with the relay off: C3 982 edges in the 500 ms pulse (~1.96 kHz, stock reported 237 V); no UART line | 16 A product class; calibration unknown | stock manufacturer `4417`, image type `54179`, file version `82` (owner capture 2026-10-14); probe board `OUTLET_BSEED_TS011F_PROBE`, image type `47116` | probe installed on the owner's outlet (2026-10-15, `0x20000201`); PA0-PA6, PB0-PB7, PC0-PC7, PD0-PD7 probed, A7 (SWS) excluded; pin map complete; remaining: calibration against a reference meter |
| TS130F `_TZ3000_vsj5o7dm` | Telink (OUI), mains router; TLSR8258/ZT-family candidate | stop/up/down, two relays and backlight all unknown | n/a unless found | stock travel/calibration behavior known; hardware values unknown | stock manufacturer `4417`, image type `54179`, file version `70`, app version 70 (owner unit 0xa4c138cee67acd17, 2026-10-15; same ids as the outlet, so only a single-device URL install can target one unit) | pins unknown; can be found with a probe board (probe + relay + switch, motor relays interlocked) instead of opening a unit |

Do not resolve unknown cells from another TS011F/TS130F profile. Record board
revision, labeled ICs, continuity/pin tracing, stock Basic/OTA descriptors and a
safe bench test before adding a database entry.

Public evidence narrows the search without confirming GPIO. The owner's units
report Basic `appVersion` 82, `stackVersion` 0, `hwVersion` 1, an empty
`dateCode` and no `swBuildId` (unsupported attribute), and the OTA query
`manufacturerCode` 4417, `imageType` 54179, `fileVersion` 82 (2026-10-14).
Image type 54179 is shared by 149 stock Tuya entries in the OTA index, so
every entry must be filtered by manufacturer name.

Stock Z2M state of the owner's outlet (`TS011F_plug_1`), the feature baseline a
replacement profile must cover or explicitly drop: `state`, `power` (W),
`voltage` (V, 236 observed), `current` (A), `energy` (kWh, cumulative),
`power_outage_memory` (restore/on/off), `indicator_mode`, `child_lock`,
`countdown` (s), `switch_type_button`, `identify`. Some TS011F BL0937 plugs
switch off above ~240 V in stock firmware; record whether this unit does
before choosing protection defaults. Its endpoint has OnOff, Electrical
Measurement and Metering. The candidate pins come from the hardware-confirmed
`_TZ3000_2uollq9d` sibling and remain unconfirmed for `drzlxjne`. The exact cover
reports app version `70`, position, moving, calibration, motor reversal and
calibration-time attributes, but no public GPIO trace.

## Module checklist

```text
[x] src/device_config/device_composition.*
[x] src/device_config/config_parser.*       parse only
[x] src/device_config/feature_wiring.*       sole composition root
[x] src/base_components/action_mapper.*      typed local mappings
[x] src/base_components/relay_controller.*   domain result, observers, inhibit
[x] src/base_components/cover_controller.*
[x] src/hal/gpio_counter.h + stub implementation; Telink/Silabs reject unsupported profiles
[x] src/base_components/energy_meter.h
[x] src/base_components/energy_measurement/bl0937.*
[x] src/base_components/meter_service.*
[x] meter construction and claimed driver pins in feature_wiring (composition claims; no gpio_manager yet)
[ ] src/device_config/device_config_service.*
[ ] src/base_components/diagnostics_service.*
[ ] src/base_components/gpio_manager.*
[ ] platform GPIO capability maps, portable IDs, checked output/cutoff and shared edge routing
[ ] src/base_components/gpio_probe_service.*
[ ] explicit ZCL access/manufacturer/context and per-cluster candidate-write callbacks
[ ] generic Telink private registration and Silabs metadata/storage/reporting alignment
[ ] src/zigbee/zcl_ids.h + cluster-owned definitions + shared management metadata
[ ] src/zigbee/device_config_cluster.*
[ ] src/zigbee/device_diagnostics_cluster.*
[ ] Basic identity-only and removal of global attribute dispatcher/record translators
[ ] src/base_components/overload_controller.*
[x] src/zigbee/cover_cluster.*               thin adapter
[x] src/zigbee/electrical_measurement_cluster.*
[x] src/zigbee/metering_cluster.*
[x] src/zigbee/meter_calibration_cluster.* (0xFC12)
[ ] Telink/Silabs/stub cluster registration
[x] capability NVM item ids and meter clearing step
[ ] management owner records/version step and incompatible-record reset tests
[x] native unit tests with fake relays/action sinks
[x] stub/pytest integration tests
[ ] generator templates and separately regenerated outputs
[ ] diagnostics capability bitmap, optional counter groups and GET_PIN_INFO
[ ] pulse request/result IDs and bounded retry cache only in output-enabled builds
[ ] independent management/recovery initialization without feature providers or LEDs
[ ] fixed management clusters with capability-gated opt-in UI; no per-pin entities
[ ] small-target flash/OTA/RAM/stack budgets and idle/sleep/resource regressions
[ ] probe service/native/stub faults and stable-descriptor coordinator tests
[ ] Telink and Silabs GPIO input/output bench acceptance
[ ] exact outlet and cover database profiles
[ ] three-device hardware/OTA evidence
```

## Decisions fixed by the architecture

| Decision | Resolution |
| --- | --- |
| Product modules | no outlet/wall-switch controllers; compose capabilities |
| Cover position | internal 0 closed / 10000 open; adapter converts wire convention |
| Reversal delay | enforced once by relay interlock; controller follows applied relay events |
| Relay observers | bounded fan-out, not a replaceable single callback |
| Controller results | domain enums; Zigbee status mapping stays in adapters |
| Meter pulses | dedicated counter HAL; never button edge transport |
| Calibration | normalized gain override in service; IC constants in driver config |
| Energy owner | `meter_service`, independent of Zigbee availability |
| Protection | persistent trip plus relay inhibit; clearing never restores ON |
| Runtime/YAML input | config strings and generated profiles normalize before wiring |
| Compatibility | no firmware, NVM, internal C API or generated-output compatibility between refactor phases; changed owner layouts are cleared, never translated |
| Management ownership | Basic = identity; Device Config = persistent global settings; Device Diagnostics = observation and bounded service operations |
| Management protocol | EP1 `0xFC10`/`0xFC11`; remove Basic private management attributes and all aliases/fallback paths in the same substage |
| Write semantics | repeatable pure validation, once-only commit; per-record ordinary results, no-response suppression, atomic undivided writes or rejection before mutation |
| Recovery boot | core management/providers before feature construction; one selected Zigbee graph; failed features leave safe outputs and unresolved claims protected |
| GPIO ownership | category + instance claims cover relay/button/LED/meter/battery/platform resources; cover motors stay owned by relay drivers |
| Probe pin identity | portable uint16 port/index ID, translated to native pins; never raw Telink/stub encodings |
| Pin metadata | bounded GET_PIN_INFO returns validity/capabilities/owner/pulse maximum without reading levels or claiming GPIO |
| Probe safety | input 1..300 s; output design ceiling 500 ms, limited by advertised per-pin bound including tolerance; early cutoff feasibility, unsupported otherwise |
| Pulse retries | sender + application request ID; at most two records through execution and five seconds after termination; repeated acceptance response, no repeated pulse; no cross-reboot guarantee |
| Probe topology | one volatile session, fixed management descriptors, no device_config edits/reboot/re-interview per probe |
| Small-device scope | mandatory small management/ownership core; independently gated counter groups, pin-info, input and output; disabled features have no runtime allocation or idle activity |
| Product UI | normal controls by default; supported diagnostics opt-in; no unrelated debug tools, absent-hardware controls, per-pin entities or automatic scanning |
| Input/counter routing | manager routes the single edge sink; diagnostics cannot steal button capture or meter counter resources |
| Protocol definitions | cluster-owned constants and one shared management metadata source; generated platform/integration parity is a gate |
| Exact device identity | no pin/IC/OTA inference from generic Tuya model names |

## Open implementation and hardware evidence

| Item | Required resolution |
| --- | --- |
| TS011F meter IC and signal interface | BL0937 pulse outputs confirmed by probe (CF C2, CF1 C3, SEL D2, SEL high = voltage); IC marking not photographed (unit cannot be opened) |
| TS011F counter routing | Telink timer0/1 GPIO-trigger routes build for TLSR8258; counts on hardware unverified (no install yet); Silabs unsupported |
| Hardware calibration defaults | voltage scale from the drzlxjne probe (1964 Hz ≈ 237 V); current/power scales from the b28wrpvx sibling (PR #491); compare against a reference meter at no load and three resistive loads, then set the scales or document per-unit gains |
| Energy checkpoint constants | endurance calculation done (see Metering design); measured loss on abrupt power cut open |
| TS130F relay/button/indicator pins | board tracing and safe low-voltage validation before mains/motor test |
| TS130F travel/overrun defaults | hardware calibration; runtime values remain user writable within bounds |
| Fail-open/fail-closed meter-loss policy | set with the Phase 8 overload controller; the meter has no relay authority now |
| Management manufacturer code and schema path | select the project protocol constant and metadata source in Phase 6.1; do not infer a stock OTA allocation |
| Small-target resource budgets | select actual lowest-resource supported builds and record baseline, allowed deltas and headroom in 6.1; verify core-only/input/output variants where supported |
| Probe capability maps | prove per-MCU/package/module reservations, optional board restrictions and supported input/IRQ/output modes |
| Bounded output on target platforms | 6.1 feasibility identifies routed resource and tolerance; 6.4 scope evidence under load/sleep/reset proves advertised maximum, otherwise output remains unsupported |
| Coordinator probe workflow | commands and reports on fixed EP1 management clusters without reconfigure/re-interview across sessions |

Detailed evidence, candidate configurations and acceptance procedures are in
[05_stage_3_metering.md](./05_stage_3_metering.md) and
[06_stage_4_protection_profiles.md](./06_stage_4_protection_profiles.md).

## Evidence log

| Date | Phase | Commit | Target | Evidence | Result |
| --- | --- | --- | --- | --- | --- |
| 2026-09-01 | 1 | `6069bb29` | `tuya-telink-build` | `make stub/build`, `make -B unit_tests`, and `make tests` passed (277); compatible Uncrustify 0.83 ran twice with an identical diff hash; `git diff --check` passed; `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed with `IMAGE_TYPE=43627`. | Phase 1 correction gate complete. |
| 2026-09-01 | 2 | `01f7ed76` | `tuya-telink-build` | Fresh `/workspace/stage2-phase2` copy: `make stub/build`, `make -B unit_tests`, and `make tests` passed (277); compatible Uncrustify 0.83 ran twice with no second-pass source diff; `git diff --check` passed on the source tree; `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed with `IMAGE_TYPE=43627` and preserved TS0726 config. Native coverage includes relay results/observers/inhibits/wrap, typed mapper fake sinks, and released-record migration fixtures. | Phase 2 complete. |
| 2026-09-05 | 3 | uncommitted | `tuya-telink-build` | `make stub/build`, `make -B unit_tests`, and `make tests` passed (277); Uncrustify 0.83 ran twice; `git diff --check` passed; `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed with `IMAGE_TYPE=43627`. Native coverage adds controller sequencing, deferred reversal replacement/stop, inhibit and timeout; the cover integration suite verifies STOPPED through the applied interlock gap. | Phase 3 complete. |
| 2026-09-05 | 4 | uncommitted | `tuya-telink-build` | `make -B unit_tests` passed; `make tests` passed (279); Uncrustify 0.83 and `git diff --check` passed. Calibrated tests cover partial GOTO, dead zones, unequal-time fallback, endpoint overrun, timer wraparound, NVM-write degradation, manual rejection, malformed payloads and invalid setting rollback. `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed with `IMAGE_TYPE=43627`, NVM migration version 7, and 197352 text / 909 data / 37518 bss. | Phase 4 complete; TS130F hardware remains blocked on pin tracing. |
| reported in hand-off; recorded 2026-09-07 | 5 software | uncommitted | `tuya-telink-build` | Hand-off reports `make -B unit_tests`, `make tests` (279), `make stub/build`, final meter regression and `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed; existing unrelated noreturn warning; formatting twice idempotent. Counter/fake-driver/service/NVM-v8 source and native test presence confirmed by code inspection. Build logs were not rechecked or tests rerun in this docs review. | Generic foundation present; exact driver, routes, wiring, calibration and endurance gates remain open. |
| 2026-09-07 | 6 planning | uncommitted | working-tree source and docs | Inspected HAL, Basic/global-write routing, all three backends, composition, GPIO/edge sink, source integration templates and Phase 5 modules. Contracts include cutoff feasibility, pin-info, pulse retry IDs, validation/commit separation, independent recovery and small-device resource/capability gates. | Phase 6 specified, not implemented or runtime-validated; resource/cutoff evidence remains pending. |
| 2026-09-09 | 6.1 callback routing | uncommitted | `tuya-telink-build` | Removed `general_commands` and the global attribute-write API; writable relay, switch, cover, cover-switch, button-event, Poll Control and Device Config clusters own validation/commit callbacks. Stub stages a candidate before mirroring and restores on rejected commit; Silabs validates its external-storage buffer before mirroring. `make tests` passed (272); formatting was idempotent; `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed at 199136 text / 645 data / 38650 bss. | Per-cluster routing is active. Telink raw candidate extraction, immutable incoming context, generated protocol parity, resource/cutoff and coordinator evidence remain open. |
| 2026-09-09 | 6.1–6.2 | uncommitted | `tuya-telink-build` | `make stub/build`, `make tests` (272), native unit binaries, formatting and `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed. The unit-test mount is non-executable; copied binaries passed from `/tmp`. The Telink image uses 198624 text / 645 data / 38654 bss. | Management/recovery implementation is validated in the stub and Telink build; remaining Phase 6 gates are open. |
| 2026-09-09 | 6.1 Telink pre-write overlay | uncommitted | `tuya-telink-build` | The tracked SDK overlay patch applied while building `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build`; the image used 199472 text / 645 data / 39294 bss. `make tests` passed (272), `make format` ran twice, and `git diff --check` passed. The build retained the existing `hal_system_reset` noreturn warning. | The overlay calls cluster validation and commit before Telink mirrors a normal write record, and rejects undivided writes before mutation. Packet-level per-record/no-response fixtures and backend parity remain open. |
| 2026-09-09 | 6.1–6.2 software follow-up | uncommitted | `tuya-telink-build` | `make -B unit_tests` passed; `make tests` passed (273); the management-cluster suite passed (5), including rejected NVM persistence; the management metadata generator/check passed; formatting was run twice; and `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed at 199472 text / 645 data / 39294 bss. | `helper_scripts/management_protocol.json` now generates and checks C, Silabs XML, test constants and integration-template fragments. Exact cluster/attribute lookup is covered in native tests and reporting rejects undeclared/non-reportable attributes. Packet captures, Silabs SDK build/runtime metadata verification, resource/capability-off measurements, cutoff proof and coordinator evidence remain open. |
| 2026-09-11 | 6.1–6.2 software follow-up | uncommitted | `tuya-telink-build` | `make stub/build`, `make -B unit_tests`, `make tests` (278), and `make tools/check_management_protocol` passed; formatting ran twice with no further source change and `git diff --check` passed. `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed at 199760 text / 645 data / 39302 bss and compiled the tracked Telink ZCL overlay. Inspection of the resulting patched SDK source verified incoming-write context clearing on parse and response-allocation early returns in ordinary, no-response and undivided write handlers. | Stub and Telink software evidence remains current. Telink packet fixtures, backend-parity fixtures, Silabs build/runtime verification, generated integration behavior, resource/capability-off measurements and all external acceptance remain open. The installed Silabs SLC project generator did not produce `zigbee.Makefile`, so `make silabs/build` could not start compilation. |
| 2026-10-15 | 6.4 GPIO probe | `05c97753` | `OUTLET_BSEED_TS011F_PROBE` | Probe OTA image (`.zigbee`) measured at 190866 B, before the index-guard, attribute-read and pull-test review fixes. | Probe image builds for TLSR8258; not flashed, no runtime evidence. |
| 2026-10-15 | 5/7 meter | `feat/bl0937-meter` | `OUTLET_BSEED_TS011F_PM_DRZLXJNE` (sk8) | `make tests` 460 passed, `make unit_tests` passed, check_device_features 0 errors; Telink `.zigbee` 200546 B (158 B under the 200704 B bootloader-layout slot); `MODULE_GIRIER_TS130F_1GANG` 199858 B and `SWITCH_BSEED_TS0726_4GANG` 192370 B, both below the integration baseline (199954 B, 192482 B) | Builds; not flashed, no hardware or coordinator evidence. |
| 2026-10-15 | 6.4 GPIO probe | `28640782` | `OUTLET_BSEED_TS011F_PROBE` | Probe OTA image (`.zigbee`) 191314 B after the review fixes and the HAL pin encoding; non-probe images unchanged in size. | Probe image builds for TLSR8258; not flashed, no runtime evidence. |

Large compiler logs, UART logs and packet captures stay outside `docs/`. Record a
stable path/link and a concise counter/result summary here. The Phase 5 hand-off
does not provide a stable log path or commit; attach those at the next code gate.
