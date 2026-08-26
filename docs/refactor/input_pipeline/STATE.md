# State

Tracking board for the input/relay pipeline refactor.

- Branch: `refactor/input-pipeline`
- Fork: `desty2k/tuya-zigbee-switch` (upstream `romasku/tuya-zigbee-switch`)
- Base commit: `bf1059ee`
- Current position: **design complete, stage 1 not started**

## Stages

| # | Stage | Status | PR | Gate |
| --- | --- | --- | --- | --- |
| 1 | GPIO edge capture | not started | — | pytest green, edge counters non-zero |
| 2 | Edge queue + worker | not started | — | `test_gpio_edge_queue`, pytest green |
| 3 | Timer service | not started | — | `test_timer_service`, pytest green |
| 4 | Button input | not started | — | `test_button_input`, `fuzz_debounce`, `test_input_latency.py` |
| 5 | Gesture layer, old button layer deleted | not started | — | `test_gesture_fsm`, `test_gesture_reset.py`, full pytest green |
| 6 | Hardware regression | not started | — | hardware matrix on Telink + Silabs |
| 7 | Relay driver + controller | not started | — | `test_relay_controller`, relay/cover suites |
| 8 | Inching + timed-off | not started | — | `test_relay_inching.py`, `test_relay_timed_off.py` |
| 9 | Interlock | not started | — | `test_interlock` (unit + pytest) |
| 10 | Zigbee event transport | not started | — | `test_button_events.py`, hardware pass 2 |

Statuses: `not started`, `in progress`, `in review`, `done`, `blocked`.

## Module checklist

```text
[ ] src/hal/gpio.h                       edge sink contract
[ ] src/telink/hal/gpio_interrupts.c     capture procedure
[ ] src/silabs/hal/gpio.c                capture procedure
[ ] src/stub/hal/gpio.c                  edge injection
[ ] src/base_components/gpio_edge_queue.*
[ ] src/base_components/button_input.*
[ ] src/base_components/button_dispatcher.*
[ ] src/base_components/gesture_fsm.*
[ ] src/base_components/timer_service.*
[ ] src/base_components/action_mapper.*
[ ] src/base_components/relay_driver.*
[ ] src/base_components/relay_controller.*
[ ] src/base_components/interlock.*
[ ] src/device_config/system_action.*
[ ] src/device_config/feature_wiring.*
[ ] src/device_config/config_parser.c    config-only parsing
[ ] src/zigbee/button_event_cluster.*
[ ] src/zigbee/switch_cluster.c          event/gesture consumer
[ ] src/zigbee/cover_switch_cluster.c    event/gesture consumer
[ ] src/zigbee/relay_cluster.c           controller client
[ ] src/zigbee/cover_cluster.c           controller client
[ ] src/zigbee/basic_cluster.c           diagnostic counters
[ ] src/hal/zigbee.h + impls             send-to-coordinator, report snapshot fix
[ ] tests/unit/**                        native unit tests
[ ] tests/conftest.py                    debounce + new helpers
[ ] tests/test_*.py                      new integration suites
[ ] .github/workflows/test.yml           run unit tests
[ ] docs/                                user-facing docs for new features
[ ] zigbee2mqtt/, zha/, homed/           regenerated converters
[ ] NVM_MIGRATIONS_VERSION               bumped with migration step
[ ] src/base_components/button.c/.h      deleted
```

## Hardware regression log

| Date | Device | Firmware | Test | Result | Counters |
| --- | --- | --- | --- | --- | --- |
| — | — | — | — | — | — |

## Open questions

| # | Question | Owner | Resolution |
| --- | --- | --- | --- |
| 1 | Final `debounce_ms` default (8 ms assumed) | — | pending stage 6 |
| 2 | Whether `0xFC02` needs a manufacturer code for Z2M discovery | — | pending stage 10 |
| 3 | `off_wait_time` support for `OnWithTimedOff` | — | out of scope unless requested |

## Decision log

Design deviations and their rationale: [DECISIONS.md](./DECISIONS.md). Append new
entries there rather than editing existing numbered items.
