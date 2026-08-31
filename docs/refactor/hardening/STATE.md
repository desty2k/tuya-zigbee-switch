# State

Tracking board for TLSR8258 router hardening.

- Branch: `refactor/input-pipeline`
- Target: `SWITCH_BSEED_TS0726_4GANG`, router build, TLSR8258
- Design baseline: `70598dca`
- Current position: **Phase-1 software baseline complete; hardware soak blocked**

## Phase 1 — stability gate

| # | Stage | Status | Gate |
| --- | --- | --- | --- |
| 1 | ISR to task isolation | done | Native deferred-scheduling and full suite pass; static Telink IRQ audit has no task, timer, Zigbee, relay or NVM call reachable from `gpio_isr_callback`; BSEED router build passes |
| 2 | Zigbee write safety and polarity | done | Invalid callback endpoint matrix and pull-derived B/S/X stub matrix pass; BSEED router build passes |
| 3 | Resource bounds | done | Shared checked reservation/append builder, parser-assigned exact storage, Telink/Silabs preflight, descriptor diagnostics and native capacity/sentinel coverage pass; BSEED graph is 8 endpoints, 25 input clusters, 9 output clusters and 120 attributes |
| 4 | Paced `0xFC02` transport | done | One-attempt queue pump, retry/expiry/high-water/submission counters, failure injection and generated diagnostics pass native and stub coverage; BSEED router build passes |
| 5 | Router liveness instrumentation | done | Poll monitor, compact transition UART records, Basic diagnostics, deterministic announce rejection/retry injection and transition-deduplication tests pass; BSEED router build passes |
| 6 | Phase-1 hardware soak | blocked | requires flashed BSEED 4-gang and coordinator capture |

## Phase-1 software verification — 2026-08-31

- `make stub/build` passed without warnings.
- `make -B unit_tests` passed, including the descriptor-capacity/sentinel and
  deferred-input fuzz coverage.
- `make tests` passed: `270 passed in 6.35s`.
- `make format` is idempotent and `git diff --check` is clean.
- `BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build` passed
  in the x86_64 `tuya-telink-build` Colima profile. The normal OTA image uses
  image type `43627`; stock and forced OTA packaging retain their existing ids.
- The profile contains Ubuntu-packaged `make`, `yq` and `python3-click`, with
  only their repository dependencies installed for the build.

The phase-1 and phase-2 hardware soak gates remain blocked until real BSEED
4-gang results are recorded.

## Phase 2 — operational hardening

| # | Stage | Status | Gate |
| --- | --- | --- | --- |
| 7 | Startup input settle | not started | breaker-cycle matrix; no boot actions |
| 8 | Rejoin resynchronization | not started | genuine transition tests; staggered reports; announce retry |
| 9 | GPIO overflow reconciliation | not started | forced overflow converges to sampled hardware state |
| 10 | OTA image-type validation | not started | duplicate fixture rejected; BSEED id unchanged |
| 11 | NVM coalescing and errors | not started | burst writes coalesced; reset flush and failure paths tested |
| 12 | Phase-2 hardware soak | blocked | requires phase-1 baseline and flashed BSEED 4-gang |

Statuses: `not started`, `in progress`, `in review`, `done`, `blocked`.

## Target-board facts

| Property | Value |
| --- | --- |
| Database key | `SWITCH_BSEED_TS0726_4GANG` |
| Product | BSEED Echo Click / Scale 4-gang |
| MCU / role | TLSR8258 / router |
| Expanded endpoints | 8: four switch endpoints and four relay endpoints |
| Telink endpoint capacity | 8; the target is an exact-capacity configuration |
| Firmware image type | `43627`; unique in the current database |
| Inputs | four pull-up switches; `M` selects momentary behaviour |

## Hardware evidence

| Date | Commit | Coordinator | Test | Duration / count | Result | Counter snapshot |
| --- | --- | --- | --- | --- | --- | --- |
| — | — | — | — | — | — | — |

## Open parameters

| Parameter | Initial design value | Resolution rule |
| --- | --- | --- |
| `0xFC02` submission interval | 40 ms | Increase only if receiver capture still shows loss with all firmware loss counters at zero |
| failed-send retry delay | 100 ms, capped by event TTL | Verify under injected submission failure and coordinator outage |
| startup settle interval | 250 ms | Scope trace on breaker power-up; use the shortest value that excludes all input transients |
| rejoin report spacing | 75 ms | Verify four relay + four button endpoints without stack saturation |
| NVM commit delay | 1000 ms | Verify interactive configuration and bounded write count |

## Decision log

Hardening choices and rationale are in [DECISIONS.md](./DECISIONS.md). Append
numbered entries rather than rewriting settled entries.
