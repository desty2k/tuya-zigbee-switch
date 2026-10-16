# Open work

Engineering work that is known and not done. Limits users can notice are in
[known_issues.md](../known_issues.md); the reasons behind the current design
are in [design_decisions.md](./design_decisions.md).

## Input

- **Startup input settle.** Edges in the first moments after power-up go
  straight to the pipeline; only an input already down when it is added is
  suppressed (boot press). A short quarantine after GPIO setup, then a fresh
  sample of every input, would keep power-up transients from switching relays
  or sending bindings.
- **`G` token range.** The parser accepts any `G` value up to 65535, while the
  multi-click gap attribute accepts 100–2000 ms
  ([config_string.md](../usage/config_string.md#tokens)). The parser should
  apply the same range.

## Relays and protection

- **Overload protection.** There is no overload controller: the meter never
  switches a relay. `relay_ctrl_inhibit()` / `relay_ctrl_release()` and
  `RELAY_SOURCE_PROTECTION` exist for it but have no caller. It needs trip
  thresholds clamped to immutable ratings, a trip delay, a persistent trip
  state, a clear that never switches a relay back on, and a policy for a
  failed meter.
- **Timer arm failures.** `timer_service` counts a task it cannot arm (Telink's
  event timer pool is shared with the stack) but does not retry it. Only the
  latching coil pulse (`relay_driver`) and the cover cutoff retry; an inching
  or timed-off deadline, an interlock dead time, a gesture timer or an LED
  blink is lost when arming fails.

## Zigbee

- **Write ranges.** The write callbacks of `switch_cluster.c` (switch mode,
  action, relay mode, binded mode, long press duration), `relay_cluster.c`
  (power-on behaviour, indicator mode) and `cover_switch_cluster.c` (all
  settings) check only the size of the value, plus the relay index and the
  interlock group. Out-of-range values are accepted and stored; the checks
  belong in the VALIDATE phase.
- **Write Attributes Undivided** validates each record against the current
  state, not against the frame's earlier records (`src/hal/zigbee.h`).
- **Telink reporting table.** `ZCL_REPORTING_TABLE_NUM` is 12, all used on a
  4-gang switch with Zigbee2MQTT's reporting configuration. Growing it changes
  the layout of the SDK's `NV_ITEM_ZCL_REPORT` and needs a migration.
- **State after a rejoin.** The device announces itself once after boot. After
  a network loss and rejoin it neither announces again nor re-reports its
  relay and button state; a paced re-report of current state (never of past
  events) would resynchronise the coordinator.
- **Network steering** starts again on every main-loop pass while not joined,
  with no backoff or jitter.
- **Button event delivery.** "Submitted" means accepted by the stack; there is
  no APS acknowledgement behind it. The `0xFC02` event command is sent without
  a manufacturer code, unlike the cluster's attributes.
- **Integrations.** ZHA and HOMEd have not been tested against a live
  installation; the ZHA quirk may emit two events when the cover position
  becomes unknown (`0xFF`).

## Persistence

- **NVM write coalescing.** Every committed attribute write and every relay
  state change with `previous`/`toggle` power-on behaviour writes flash at
  once. Delayed, per-item coalesced commits would cut flash wear and main-loop
  stalls during bursts of writes.
- **NVM write failures** are mostly ignored (`capability_state_store_*()`
  results are discarded) and not counted in the diagnostics.
- **OTA image types.** `device_db.yaml` contains `firmware_image_type` values
  shared by several boards, and no check rejects a new duplicate. A CI check
  should refuse new collisions; renumbering existing boards is an OTA
  compatibility change.

## GPIO

- **Pin ownership.** There is no GPIO ownership table. The composition
  rejects a pin used twice by buttons, relays, the battery input and meters
  (LED pins are left out of that check), but pins the platform needs are not
  reserved in it, and nothing answers "who owns this pin" at runtime.
  `button_input` installs the single HAL edge sink; the probe counts edges
  through its own HAL (`src/hal/gpio_probe.h`).
- **Probe output cutoff** runs from the main loop; a stalled loop is bounded
  only by the 1 s watchdog. There is no hardware-timed cutoff.

## Platforms

- **Silabs.** EFR32MG21/MG22 images build but have not run 2.x on hardware.
  There is no Silabs GPIO counter backend, so the meter is Telink only.
  `MODULE_SONOFF_ZBMINIL2` (EFR32MG22) overflows its RAM and does not build.

## Hardware tests not run

- **Input regression matrix** on a Telink touch switch and a Silabs device:
  100 normal and 100 fast clicks, bursts of 2, 3, 5 and 10 clicks, 20–50 ms
  taps, holds of 1–10 s, ten-click reset, no presses under mains load for
  10 minutes, presses while an end device sleeps. The 25 ms debounce default
  awaits this measurement.
- **Multi-gang stress**: overlapping presses on two and four gangs, with the
  received `0xFC02` sequence compared against the presses and all loss
  counters at zero.
- **Router soak**: several days on one build, with coordinator outages of 1,
  5 and 30 minutes, recovery without a button press, and no unexplained
  reset.
- **Power cycles**: 50 breaker cycles per power-on behaviour, with released and
  held buttons, checking that no relay change, binding or button event comes
  from the power-up itself.
- **Cover**: 100 reversals, stop during the dead time, the manual-mode safety
  cutoff, unequal travel times, positions 0/25/50/75/100 %, and power loss
  while moving.
- **Meter**: comparison with a reference meter at no load and three resistive
  loads (the current and power scales are not measured on the unit), a 24 h
  energy comparison, and the energy lost on an abrupt power cut.
