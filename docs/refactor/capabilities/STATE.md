# State

Tracking board for the BSEED capability architecture.
The target contracts and gates are in [01_architecture.md](./01_architecture.md)
and [02_phases.md](./02_phases.md).

- Branch: `refactor/input-pipeline`
- Design baseline: `505b82c3`
- Created: 2026-09-01
- Current position: **Phase 1 implementation complete; Phase 2 in progress**

## Phases

| # | Phase | Status | Gate |
| --- | --- | --- | --- |
| 1 | Normalized composition root | done | pure parser and normalized composition root verified in `tuya-telink-build` |
| 2 | Capability-safe relay and action contracts | in progress | native contracts + existing switch/relay suites |
| 3 | Cover domain extraction | not started | controller unit tests + cover regressions |
| 4 | Cover position and calibration | not started | virtual-time/NVM/Zigbee tests + TS130F hardware |
| 5 | Meter HAL, driver and service | not started | software may proceed; completion needs exact outlet hardware/IC confirmation |
| 6 | Zigbee measurement adapters | not started | conversion/stub tests + reference-meter capture |
| 7 | Overload and indicator relations | not started | protection/priority tests + trip hardware test |
| 8 | BSEED profiles and release gate | blocked | exact outlet/cover profiles and all hardware evidence |

Statuses: `not started`, `in progress`, `in review`, `done`, `blocked`.

## Validated repository facts

| Area | Current fact |
| --- | --- |
| Input/output foundation | input pipeline, timer service, relay driver/controller, interlock and indicator feedback exist |
| Composition | `feature_wiring` exists, but `config_parser` still performs initialization and endpoint/Zigbee construction |
| Relay API | returns `hal_zigbee_cmd_result_t`; one state callback per relay; no protection source/inhibit |
| Cover | behavior and NVM live in `cover_cluster`; no GOTO; position is a global placeholder 50 |
| Metering | no counter HAL, meter driver/service, Electrical Measurement or Metering adapter in application code |
| Device schema | flat legacy metadata plus `config_str`; no capability/profile objects |
| TS0726 target | key `SWITCH_BSEED_TS0726_4GANG`, TLSR8258 router, image type `43627`, eight endpoints |
| Exact outlet fingerprint | `_TZ3210_drzlxjne` absent from `device_db.yaml` |
| Exact cover fingerprint | `_TZ3000_vsj5o7dm` absent from `device_db.yaml` |

## Target hardware characterization

| Device | MCU/module | Relay/button/LED pins | Meter IC/pins | Ratings/calibration | OTA ids | Status |
| --- | --- | --- | --- | --- | --- | --- |
| TS0726 `_TZ3002_pzao9ls1` | ZTU / TLSR8258 | known in existing config | none known | n/a | stock `54179`, firmware `43627` | profile present |
| TS011F `_TZ3210_drzlxjne` | ZTU/TLSR8258 candidate | sibling candidate: LED C3, button B5, relay D2, indicator B4 | sibling candidate: CF C0, CF1 C2, SEL C1; exact IC unknown | 16 A product class; exact calibration unknown | reported stock manufacturer `4417`, image `54179`; new firmware id unassigned | blocked: open and trace exact unit |
| TS130F `_TZ3000_vsj5o7dm` | TLSR8258/ZT-family candidate | stop/up/down, two relays and backlight all unknown | n/a unless found | stock travel/calibration behavior known; hardware values unknown | unknown | blocked: open and trace exact unit |

Do not resolve unknown cells from another TS011F/TS130F profile. Record board
revision, labeled ICs, continuity/pin tracing, stock Basic/OTA descriptors and a
safe bench test before adding a database entry.

Public evidence narrows the search without confirming GPIO. The exact outlet
reports Basic application version `192`, while its OTA record reports file
version `82`; these are different fields. Its endpoint has OnOff, Electrical
Measurement and Metering. The candidate pins come from the hardware-confirmed
`_TZ3000_2uollq9d` sibling and remain unconfirmed for `drzlxjne`. The exact cover
reports app version `70`, position, moving, calibration, motor reversal and
calibration-time attributes, but no public GPIO trace.

## Module checklist

```text
[x] src/device_config/device_composition.*
[x] src/device_config/config_parser.*       parse only
[x] src/device_config/feature_wiring.*       sole composition root
[ ] src/base_components/action_mapper.*      typed local mappings
[ ] src/base_components/relay_controller.*   domain result, observers, inhibit
[ ] src/base_components/cover_controller.*
[ ] src/hal/gpio_counter.h + 3 implementations
[ ] src/base_components/energy_meter.h
[ ] src/base_components/energy_measurement/<confirmed-driver>.*
[ ] src/base_components/meter_service.*
[ ] src/base_components/overload_controller.*
[ ] src/zigbee/cover_cluster.*               thin adapter
[ ] src/zigbee/electrical_measurement_cluster.*
[ ] src/zigbee/metering_cluster.*
[ ] Telink/Silabs/stub cluster registration
[ ] NVM item ids and explicit migrations
[ ] native unit tests with fake relays/meters
[ ] stub/pytest integration tests
[ ] generator templates and separately regenerated outputs
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
| Runtime/YAML migration | legacy strings remain; both inputs normalize before wiring |
| Exact device identity | no pin/IC/OTA inference from generic Tuya model names |

## Open evidence, not open architecture

| Item | Required resolution |
| --- | --- |
| TS011F meter IC and signal interface | verify marking and candidate C0/C2/C1 signals before Phase 5 driver selection |
| TS011F counter routing | prove selected Telink/Silabs peripheral resources at target build and bench |
| Hardware calibration defaults | reference-meter measurements across representative voltage/load points |
| Energy checkpoint constants | platform endurance calculation plus measured maximum loss on abrupt power cut |
| TS130F relay/button/indicator pins | board tracing and safe low-voltage validation before mains/motor test |
| TS130F travel/overrun defaults | hardware calibration; runtime values remain user writable within bounds |
| Fail-open/fail-closed meter-loss policy | set explicitly in the characterized outlet profile |

Detailed evidence, candidate configurations and acceptance procedures are in
[05_stage_3_metering.md](./05_stage_3_metering.md) and
[06_stage_4_protection_profiles.md](./06_stage_4_protection_profiles.md).

## Evidence log

| Date | Phase | Commit | Target | Evidence | Result |
| --- | --- | --- | --- | --- | --- |
| — | — | — | — | — | — |
| 2026-09-01 | 1 | uncommitted | `tuya-telink-build` | Installed GCC, pytest and uncrustify in the Colima profile; `make stub/build` and `make -B unit_tests` pass, and `make tests` reports 270 passed in 42.99 s. | Phase 1 behavior preserved; final format, diff, board build and Phase 2 verification remain pending. |

Large compiler logs, UART logs and packet captures stay outside `docs/`. Record a
stable path/link and a concise counter/result summary here.
