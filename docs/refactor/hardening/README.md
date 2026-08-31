# TLSR8258 router hardening

Design specification for making `refactor/input-pipeline` a boringly reliable
router firmware for the BSEED Echo Click / Scale 4-gang
(`SWITCH_BSEED_TS0726_4GANG`). The work is intentionally limited to correctness,
bounded resource use, observability and recovery. It is not an upstream feature
adoption programme.

Documents describe the target state. Implementation order and release gates are
in [13_migration.md](./13_migration.md); live progress belongs in
[STATE.md](./STATE.md).

## Phases

| Phase | Release meaning | Problems |
| --- | --- | --- |
| 1 — stability gate | Required before the branch is called stable or installed broadly | ISR/task isolation; paced `0xFC02`; safe Zigbee write dispatch and pull-derived polarity; endpoint/cluster/attribute bounds; router liveness diagnostics and soak |
| 2 — operational hardening | Required after the phase-1 baseline is proven, before treating unattended recovery and long-term maintenance as complete | startup input settle; rejoin resynchronization; GPIO overflow reconciliation; OTA image-type validation; NVM write coalescing and failures |

Phase 2 does not weaken the phase-1 gate. A phase-1 soak must run on a build that
already contains every phase-1 code change.

## Documents

| Doc | Content |
| --- | --- |
| [01_architecture.md](./01_architecture.md) | Scope, execution model, failure boundaries and board constraints |
| [02_isr_task_isolation.md](./02_isr_task_isolation.md) | IRQ capture only; all task scheduling from normal context |
| [03_button_event_transport.md](./03_button_event_transport.md) | Paced `0xFC02` queue, retries, TTL and transport counters |
| [04_zigbee_write_safety_and_polarity.md](./04_zigbee_write_safety_and_polarity.md) | NULL-safe trampolines and pull-derived button polarity |
| [05_resource_bounds.md](./05_resource_bounds.md) | Endpoint tables and bounded cluster/attribute construction on Telink |
| [06_router_liveness.md](./06_router_liveness.md) | Network/reset diagnostics and the issue-255 soak gate |
| [07_startup_input_settle.md](./07_startup_input_settle.md) | Suppression of startup input transients and issue-460 regression |
| [08_rejoin_resynchronization.md](./08_rejoin_resynchronization.md) | Genuine rejoin detection, staggered state reports and announce retry |
| [09_gpio_overflow_reconciliation.md](./09_gpio_overflow_reconciliation.md) | Resampling watched pins after an edge-ring overflow |
| [10_ota_image_type_validation.md](./10_ota_image_type_validation.md) | CI uniqueness check without bulk image renumbering |
| [11_nvm_write_coalescing.md](./11_nvm_write_coalescing.md) | Delayed dirty commits and write-failure diagnostics |
| [12_testing.md](./12_testing.md) | Unit, stub, Telink hardware and multi-day soak tests |
| [13_migration.md](./13_migration.md) | Ordered implementation stages and acceptance gates |
| [DECISIONS.md](./DECISIONS.md) | Hardening decisions and rationale |
| [STATE.md](./STATE.md) | Progress and hardware evidence |

## Stability definition

The branch is stable only when all phase-1 software gates pass and the exact
BSEED 4-gang completes the phase-1 hardware matrix and soak without:

- an ISR calling a generic task/timer API;
- a received-event gap not explained by an exposed counter;
- an out-of-bounds or truncated Zigbee registration;
- an unexplained reset or network-unavailable interval.

The counters are evidence, not a substitute for correctness. A run with nonzero
loss, send-failure, bounds-failure or reset counters fails unless the test
deliberately injected that condition.

## Explicitly out of scope

Scenes, rotary encoders, power monitoring, new companion endpoints, a second
action model, Silabs network/watchdog work, end-device reporting optimisation,
and bulk OTA image-id migrations are outside this design.
