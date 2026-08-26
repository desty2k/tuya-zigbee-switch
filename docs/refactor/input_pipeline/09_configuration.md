# 09 — Configuration and wiring

## Separation of concerns

- `config_parser` parses the device config string and **only fills configuration
  structs**. It registers no callbacks and creates no feature behaviour.
- `feature_wiring` runs after parsing and builds the pipeline: creates buttons,
  gesture configs, action-mapper entries, relay controllers, interlock groups and
  Zigbee endpoints.
- Runtime state lives in separate structs from configuration
  (`button_config_t` / `button_runtime_t`, `relay_config_t` / `relay_runtime_t`,
  `gesture_config_t` / `gesture_runtime_t`).

Wiring order:

```text
1. device_config_read_from_nv()
2. parse_config()                  -> config structs only
3. timer_service / button_input / gesture_fsm / relay_controller init
4. relay_driver + relay_controller registration, interlock groups
5. button_input_add() per configured button
6. gesture_fsm_add() per configured button
7. cluster construction (loads persisted attributes, registers sinks)
8. action_mapper table build
9. hal_zigbee_init()
10. startup relay restore
```

## Config string tokens

Existing tokens keep their meaning:

| Token | Meaning |
| --- | --- |
| `B<pin><pull>` | on-board button; `hold_ms = 2000`; mapped to factory reset |
| `S<pin><pull>` | switch button + switch endpoint; `hold_ms = 800` |
| `R<pin>[<off_pin>]` | relay; with `off_pin` it is a latching relay |
| `X<open><close><pull>` | cover switch endpoint (two buttons) |
| `C<open><close>` | cover endpoint (two relays) |
| `L<pin>[i]` | dedicated status LED |
| `I<pin>[i]` | indicator LED |
| `BT<pin>` | battery measurement pin |
| `M` | all switch endpoints default to momentary |
| `SLP` | allow simultaneous latching coil pulses |
| `D<N>` | global debounce in ms, applies to all buttons |
| `i<N>` | OTA image type |

New tokens:

| Token | Meaning | Default |
| --- | --- | --- |
| `G<N>` | global multi-click gap in ms for all gesture-enabled buttons | 350 |
| `K<mask>[:<dead_ms>]` | interlock group; `mask` is hex over logical relay ids (`K07` = relays 0,1,2); repeatable | none |

Cover endpoints implicitly create an interlock group over their relay pair with
`dead_time_ms = 200`; no token is needed.

## ZCL configuration attributes

On/Off Switch Configuration (`0x0007`), per switch endpoint — existing:

| Id | Name |
| --- | --- |
| `0x0000` | switch type (read-only mirror) |
| `0x0010` | switch actions |
| `0xff00` | switch mode |
| `0xff01` | relay mode |
| `0xff02` | relay index |
| `0xff03` | long-press duration → `hold_ms` |
| `0xff04` | level move rate |
| `0xff05` | binding mode |

Button Event cluster (`0xFC02`) adds `MultiClickGap` (`0x0002`) and `DebounceMs`
(`0x0003`) per endpoint. On/Off (`0x0006`) per relay endpoint adds
`InchingDuration` (`0xff03`, uint16 ms, `0` disables) and `InterlockGroup`
(`0xff04`, uint8). Basic (`0x0000`) keeps `MultiPressResetCount` (`0xff02`) as
the N-click factory-reset threshold and adds the read-only diagnostic counters
listed in [08_zigbee_events.md](./08_zigbee_events.md).

## Persistence

| Persisted | Not persisted |
| --- | --- |
| Device config string | raw edges, edge queue |
| Switch endpoint config (incl. `hold_ms`, `multi_click_gap_ms`, `debounce_ms`) | `press_id`, click counters, pending N-click |
| Relay endpoint config (`startup_mode`, indicator mode/state, `inching_ms`, `interlock_group`) | gesture timers, auto-off timers |
| Relay on/off state when `StartUpOnOff` is `TOGGLE` or `PREVIOUS` | Zigbee button event queue, event `seq` |
| Cover switch endpoint config | diagnostic counters |
| `MultiPressResetCount` | button state (re-read from GPIO at boot) |

## NVM migration

`zigbee_switch_cluster_config` gains `multi_click_gap_ms` and `debounce_ms`
appended at the end; `zigbee_relay_cluster_config` gains `inching_ms` and
`interlock_group` appended at the end. Because `hal_nvm_read` is size-exact,
`NVM_MIGRATIONS_VERSION` is bumped and a migration step deletes the old switch and
relay config items so that the new defaults are applied on first boot after the
update. Every other NVM item is untouched.
