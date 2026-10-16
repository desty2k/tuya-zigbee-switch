# Configuration

Every setting below is stored on the device and survives reboots, power cuts
and OTA updates between fork 2.x releases. Settings live on the endpoint they
belong to; the endpoint layout is in [endpoints.md](./endpoints.md).

## Names in the integrations

Zigbee2MQTT names each endpoint by its kind and position:

| Count of that kind | Names |
| --- | --- |
| 1 | `switch`, `relay`, `cover_switch`, `cover` |
| 2 | `switch_left`, `switch_right` (likewise `relay_…`, `cover_switch_…`, `cover_…`) |
| 3 | `…_left`, `…_middle`, `…_right` |
| 4 or more | `switch_0` … `switch_<n-1>`, `relay_0` … (counted from 0); `cover_switch_1` … (counted from 1) |

A setting is named `<endpoint>_<setting>` in the Zigbee2MQTT UI, and its MQTT
property carries the endpoint again: the relay mode of `switch_left` is set
with `{"switch_left_relay_mode_switch_left": "detached"}`. The tables below use
the short form `<switch>_relay_mode`.

Device-wide settings (Device Config on endpoint 1) appear on the first
endpoint, for example `child_lock_switch_0` on a 4-gang switch.

ZHA shows the same settings with the endpoint number appended ("Relay mode 5").
HOMEd names them in camelCase per endpoint (`relayMode`); HOMEd numbers 4 or
more endpoints of a kind from 1 (`switch_1` …). Where an integration lacks a
setting the table says so.

## Switch endpoints (`S` buttons)

| Setting | Zigbee2MQTT | ZHA / HOMEd | Values | Default |
| --- | --- | --- | --- | --- |
| Switch type | `<switch>_mode` | Switch mode / `switchMode` | `toggle`, `momentary`, `momentary_nc` | `toggle`; `momentary` when the config string has `M` |
| Action mode | `<switch>_action_mode` | Switch actions / `switchAction` | `on_off`, `off_on`, `toggle_simple`, `toggle_smart_sync`, `toggle_smart_opposite` | `toggle_simple` |
| Relay mode | `<switch>_relay_mode` | Relay mode / `relayMode` | `detached`, `press_start`, `short_press`, `long_press`, `deferred_single` (HOMEd lacks `deferred_single`) | `short_press`; `detached` when the switch has no relay |
| Relay index | `<switch>_relay_index` | Relay index / `relayIndex` | `relay_1` … `relay_<n>` | the n-th switch drives the n-th relay endpoint |
| Binded mode | `<switch>_binded_mode` | Binded mode / `bindedMode` | `press_start`, `short_press`, `long_press` | `short_press` |
| Long press duration | `<switch>_long_press_duration` | Long press mode / `longPressDuration` | 0–5000 ms; 0 turns long presses off | 800 ms |
| Level move rate | `<switch>_level_move_rate` | Level move rate / `levelMoveRate` | 1–255 level units per second | 50 |
| Multi-click gap | `<switch>_multi_click_gap` | Multi click gap / `multiClickGap` | 100–2000 ms | 350 ms, or the config's `G` value |
| Debounce | `<switch>_debounce` | Debounce / `debounce` | 1–200 ms | 25 ms, or the config's `D` value |

Read-only: `<switch>_press_action` (`released`, `press`, `long_press` for
momentary inputs; `position_on`, `position_off` for toggle inputs).

### Switch type

- `toggle`: a rocker or latching switch. Closing the contact is the "on"
  position, opening it the "off" position. Every position change acts at once:
  the relay modes `press_start`, `short_press` and `long_press` and the binded
  modes do not apply (see [relay mode](#relay-mode) for `detached` and
  `deferred_single`).
- `momentary`: a push button that closes while pressed.
- `momentary_nc`: a push button that opens while pressed.

### Action mode

What a press (momentary) or a position (toggle) does to the relay and which
On/Off command goes to bound devices.

| Action mode | Momentary press | Toggle: on position / off position | Command to bindings |
| --- | --- | --- | --- |
| `on_off` | relay on | relay on / relay off | On / Off |
| `off_on` | relay off | relay off / relay on | Off / On |
| `toggle_simple` | toggle relay | toggle relay on every change | Toggle |
| `toggle_smart_sync` | toggle relay | toggle relay on every change | On or Off, matching the relay's new state |
| `toggle_smart_opposite` | toggle relay | toggle relay on every change | On or Off, opposite to the relay's new state |

With `toggle_smart_*` and no relay to follow, bindings receive Toggle. For
push buttons, use one of the `toggle_*` modes: `on_off` makes every press
switch the relay on.

### Relay mode

When a momentary input switches its relay:

| Relay mode | The relay switches |
| --- | --- |
| `detached` | never; the button only sends commands and [button events](./button_events.md) |
| `press_start` | when the button is pressed (fastest) |
| `short_press` | when the button is released, unless the press became a long press |
| `long_press` | when the press reaches the long press duration |
| `deferred_single` | after a single click is resolved: released, and no second press within the multi-click gap |

`deferred_single` keeps double, triple and longer clicks free for automations:
only a single click switches the relay, at the cost of the multi-click gap as
added delay. `deferred_single` is meant for momentary inputs.

On a toggle input, `detached` keeps the position changes from switching the
relay. With `deferred_single` a position change does not switch it either;
only a quick on-and-off flick (shorter than the long press duration) followed
by the multi-click gap counts as a single click and switches the relay. Bound
devices receive a command on every position change in both modes.

A switch whose relay mode is `detached` (or that has no relay) flashes its
indicator LED briefly on each press and 2 or 3 times for a double or triple
click.

### Binded mode and long press dimming

`<switch>_binded_mode` picks the moment the On/Off command goes to bound
devices for a momentary input: on press (`press_start`), on release of a short
press (`short_press`) or when a long press starts (`long_press`).

Independently of the binded mode, holding a momentary button past the long
press duration sends Level Control *Move with On/Off* to bound devices at
`<switch>_level_move_rate`, and releasing it sends *Stop with On/Off*. The
direction alternates with every hold, starting upwards. Bind the switch
endpoint's Level Control cluster to a dimmable light to dim it from the wall
switch. A toggle input sends no level commands.

### Long press duration, multi-click gap and debounce

- The long press duration is the hold time after which a press is a long press
  (relay mode `long_press`, binded mode `long_press`, dimming, `hold` button
  events). 0 disables long presses. The firmware accepts up to 65535 ms; the
  integrations offer 0–5000 ms.
- The multi-click gap is the longest pause between releases that still counts
  towards the same multi-click.
- Debounce is how long an input must stay stable before a change counts. The
  default 25 ms is 2.5 times a 50 Hz half-cycle, so mains pickup on long switch
  wires does not register as presses.

Gap and debounce are attributes of the Button Event cluster; details in
[button_events.md](./button_events.md).

## Relay endpoints (`R` relays)

| Setting | Zigbee2MQTT | ZHA / HOMEd | Values | Default |
| --- | --- | --- | --- | --- |
| State | `state_<relay>` | switch / `status` | `ON`, `OFF` | — |
| Power-on behaviour | `power_on_behavior_<relay>` | start-up behaviour / `powerOnStatus` | `off`, `on`, `toggle`, `previous` | `off` |
| Inching duration | `<relay>_inching_duration` | Inching duration / `relayInchingDuration` | 0–65535 ms; 0 = off | 0 |
| Interlock group | `<relay>_interlock_group` | Interlock group / `relayInterlockGroup` | 0 = none, or a `K` group number | the relay's `K` group, else 0 |
| Indicator mode | `<relay>_indicator_mode` | Relay Led mode / `relayIndicatorMode` | `same`, `opposite`, `manual` | `same` |
| Indicator state | `<relay>_indicator` | Relay led state / `relayIndicator` | `ON`, `OFF` | follows the relay |

### Power-on behaviour

What the relay does when the device starts: `off`, `on`, `toggle` (the
opposite of its state before the power cut) or `previous` (the state before
the power cut). With `toggle` and `previous` the device stores the relay state
on every change.

### Inching

With an inching duration set, every switch-on, from any source (button,
Zigbee, power-on behaviour), turns the relay off again after that time. Use it
for impulse loads such as a garage door opener. An *On With Timed Off* command
keeps its own on time but never runs past the inching duration.

### Interlock

Relays in one interlock group are never on together: switching one on first
switches the others off and waits the group's dead time (200 ms unless the `K`
token sets another) after the last one went off. Groups are defined by `K`
tokens in the [config string](./config_string.md#k--relay-interlock), numbered
1, 2, … in config order.

- A relay listed in a `K` group stays in it: the only value it accepts is that
  group's number.
- Any other relay can join one of the `K` groups (1 up to the number of `K`
  tokens) or leave it again with 0.
- A device without `K` tokens accepts only 0. The integrations offer 0–10, the
  most `K` tokens a config may have.

The two relays of a cover form their own interlock group; it cannot be
changed.

Only boards with more than one relay, a `K` token or a cover have the setting:
the firmware of a single-relay board leaves interlock out, as a single relay
has nothing to interlock with.

### Relay indicator LED

Relays get the `I` LEDs of the config string in order: the first relay
endpoint takes the first `I` LED, and so on. A relay without an LED has no
indicator settings.

- `same`: LED on while the relay is on.
- `opposite`: LED on while the relay is off (finds the switch in the dark).
- `manual`: the LED shows `<relay>_indicator` and ignores the relay.

The switch endpoint with the same position uses that LED for its press
flashes. While the device is not joined to a network, the first four indicator
LEDs blink when the board has no dedicated status LED (`L`).

## Cover endpoints (`C` covers)

| Setting | Zigbee2MQTT | ZHA / HOMEd | Values | Default |
| --- | --- | --- | --- | --- |
| Open / close / stop, position | `state_<cover>`, `position_<cover>` | cover / `cover` | `OPEN`, `CLOSE`, `STOP`; 0–100 % (100 = open) | — |
| Open time | `<cover>_open_time` | Cover open time / `coverOpenTime` | 0–429496 ms | 0 |
| Close time | `<cover>_close_time` | Cover close time / `coverCloseTime` | 0–429496 ms | 0 |
| Closed deadzone | `<cover>_closed_deadzone` | Cover closed deadzone / `coverClosedDeadzone` | 0–99.99 % | 0 |
| Open deadzone | `<cover>_open_deadzone` | Cover open deadzone / `coverOpenDeadzone` | 0–99.99 % | 0 |
| Overrun | `<cover>_overrun` | Cover overrun / `coverOverrun` | 0–65535 ms | 0 |
| Motor reversal | `<cover>_motor_reversal` | Cover motor reversal / `coverMotorReversal` | on / off | off |

Read-only: `<cover>_moving` (`stopped`, `opening`, `closing`).

### How a cover moves

- **Without travel times** (both 0) the cover has no position. Open and close
  run the motor until stop, the opposite command, or a 120 s safety cutoff.
  Going to a percentage is refused.
- **With travel times** the cover tracks its position. When only one time is
  set, it is used for both directions. Open and close run for the full travel
  from the current position plus the overrun; a stop keeps an estimated
  position; *Go to lift percentage* moves to that position.
- **Unknown position**: after the first boot, after power was lost while the
  motor ran, or after the safety cutoff. Open and close then run the full
  travel time plus overrun, and the completed move makes the position known.
  Going to an intermediate percentage is refused until then. Zigbee2MQTT keeps
  showing the last position, ZHA shows none, HOMEd shows an out-of-range value
  (see [known_issues.md](../known_issues.md)).
- **At an end**: open or close towards the end the cover rests at does nothing
  (the relay stays off) when a completed move reached that end. When the cover
  was stopped at that end, the command runs only the overrun, or nothing
  without one.
- **Deadzones** are the stretches of travel next to each end that still read as
  fully closed or fully open (slats that tilt before the shutter lifts, the
  last centimetres before the stop). The two together must stay below 100 %;
  a write that breaks this is rejected.
- **Overrun** is added to every move to an end, so the motor reaches its limit
  switch even when the position estimate drifted.
- **Motor reversal** swaps the open and close relays when the motor is wired
  the other way round.

Changing any of these settings stops a moving cover. The device stores the
position whenever a move ends.

### Calibrating a cover

1. Leave the deadzones and overrun at 0 and set both travel times to 0.
2. Close the cover fully and wait until the motor stops at its limit switch.
3. Open it and time the run from start to the open limit switch. Close it and
   time the run to the closed limit switch.
4. Set `<cover>_open_time` and `<cover>_close_time` to the measured times in
   milliseconds.
5. Run a full close and a full open: the position becomes known (0 % and
   100 %).
6. If the cover reaches its ends early or late, set `<cover>_overrun` to a few
   seconds (for example 2000 ms) so every move to an end finishes at the limit
   switch.
7. If the first or last part of the travel does not change what you see, set
   the deadzones to that share of the travel, for example 8 % when slats tilt
   during the first 8 % of an opening run.
8. Check intermediate positions (50 %) and repeat with adjusted times if they
   are off.

## Cover switch endpoints (`X` inputs)

| Setting | Zigbee2MQTT | ZHA / HOMEd | Values | Default |
| --- | --- | --- | --- | --- |
| Switch type | `<cover_switch>_type` | Cover switch type / `coverSwitchType` | `toggle`, `momentary` | `momentary` |
| Invert | `<cover_switch>_invert` | Cover switch reversal / `coverSwitchReversal` | on / off | off |
| Cover index | `<cover_switch>_cover_index` | Cover switch cover index / `coverIndex` | `detached`, `cover_1` … | the n-th cover switch drives the n-th cover |
| Local mode | `<cover_switch>_local_mode` | Cover switch local mode / `coverLocalMode` | `immediate`, `short_press`, `long_press`, `hybrid` | `hybrid` |
| Binded mode | `<cover_switch>_binded_mode` | Cover switch binded mode / `coverBindedMode` | same as local mode | `hybrid` |
| Long press duration | `<cover_switch>_long_press_duration` | Cover switch long press duration / `longPressDuration` | 0–5000 ms | 800 ms |
| Multi-click gap | `<cover_switch>_multi_click_gap` | Multi click gap / `multiClickGap` | 100–2000 ms | 350 ms, or the config's `G` value |
| Debounce | `<cover_switch>_debounce` | Debounce / `debounce` | 1–200 ms | 25 ms, or the config's `D` value |

Read-only: `<cover_switch>_press_action` (`released`, `open`, `close`, `stop`,
`long_open`, `long_close`).

The local mode drives the device's own cover (when the cover index names
one); the binded mode drives covers bound to the endpoint's Window Covering
client. Both apply to momentary inputs only:

| Mode | Behaviour |
| --- | --- |
| `immediate` | open or close on press |
| `short_press` | open or close on release |
| `long_press` | open or close when the press reaches the long press duration |
| `hybrid` | a short press opens or closes fully; holding moves the cover while held and stops it on release |

A short press in the direction the cover is already moving stops it.
Pressing open and close together, or the stop button (`X` token with a third
pin), stops the cover.

A `toggle` cover switch is a two-way rocker: a closed open or close contact
moves the cover, and releasing both stops it. `invert` swaps the open and
close inputs; motor reversal on the cover swaps the motor relays.

## Meter (`E` token, Telink only)

| Setting | Zigbee2MQTT | ZHA / HOMEd | Values | Default |
| --- | --- | --- | --- | --- |
| Voltage calibration | `voltage_calibration` | Voltage calibration / `voltageCalibration` | 50–150 % | 100 % |
| Current calibration | `current_calibration` | Current calibration / `currentCalibration` | 50–150 % | 100 % |
| Power calibration | `power_calibration` | Power calibration / `powerCalibration` | 50–150 %; also scales energy | 100 % |
| Reset energy | `reset_energy` | Reset energy / — | `reset` | — |

Readings: `voltage` (V), `current` (A), `power` (W), `energy` (kWh), all on
endpoint 1. The meter samples once per second; Zigbee2MQTT configures a report
at least every 5 minutes. Energy is stored every 5 minutes at most, after
50 Wh, or once an hour, so a power cut loses at most the energy since the last
store.

To calibrate, connect a resistive load (a kettle, a heater) and a reference
meter, then for each quantity set

```
new calibration = reference value / device reading × current calibration
```

For example, the device reads 245 V, the reference 237 V, calibration 100 %:
set `voltage_calibration` to 237 / 245 × 100 = 96.73 %.

## Device-wide settings (endpoint 1)

| Setting | Zigbee2MQTT | ZHA / HOMEd | Values | Default |
| --- | --- | --- | --- | --- |
| Device config | `device_config` | — / — | config string, at most 127 characters | the board's config string |
| Status LED mode | `status_led_mode` | Status LED mode / `statusLedMode` | `network`, `off`, `on` | `network` |
| Multi-press reset count | `multi_press_reset_count` | Multi press reset count / `multiPressResetCount` | 0–10; 0 = off | 10 |
| Child lock | `child_lock` | Child lock / `childLock` | `LOCK`, `UNLOCK` | `UNLOCK` |

Read-only: `device_config_status` (`ready`, `reboot_pending`, `error`),
`device_config_last_error` (see [config_string.md](./config_string.md#when-a-config-string-is-rejected))
and the diagnostics counters (see [faq.md](../faq.md#how-do-i-read-the-diagnostics-counters)).

### Device config

The text of the [config string](./config_string.md). Writing a valid string
stores it and restarts the device about 300 ms later with the new layout; an
invalid string is refused and the device keeps its current one. Only
Zigbee2MQTT exposes it.

### Status LED mode

Only boards with a dedicated status LED (`L` token) have this setting; on
other boards a write is refused. While the device
is not joined, the status LED blinks (500 ms on, 500 ms off) in every mode.
While joined, `off` turns it off; `network` and `on` keep it on.

### Multi-press reset count

Pressing any switch or cover-switch button this many times in a row, with
pauses shorter than the multi-click gap, resets the device. A rocker (`toggle`
type) counts one press per on-and-off cycle. 0 disables it. The firmware
accepts 0–10; the integrations offer 0–255 and a value above 10 is refused. The reset works while the child lock is on.

### Factory reset

A factory reset makes the device leave the network so it can pair again, and
releases the child lock. Trigger it with:

- the multi-press reset above (10 presses by default), or
- an onboard reset button (`B` token): hold it for 2 s.

## Child lock

`child_lock` makes the device ignore its local switch and cover-switch
buttons, including a cover's stop button: a press that starts while locked
does not switch a relay, move a cover, send a command to bindings or send
[button events](./button_events.md) to the coordinator, so automations on
button actions are locked too; `ButtonState` stays released. Zigbee commands
(On/Off, Window Covering) and setting writes keep working, and the lock
survives reboots, power cuts and OTA updates.

A factory reset releases the lock: the multi-press reset still counts presses
while locked, and an onboard button's 2 s hold works as usual, so a locked
device can always be reset and paired again.

The integrations read the lock when the device is configured and on request
(Zigbee2MQTT `get`). No attribute reporting is configured for it; a lock
toggled on the device sends one report to the coordinator, so Zigbee2MQTT, ZHA
and HOMEd show the change at once. A write over Zigbee sends no report.

The lock is also toggled on the device:

| Inputs | Gesture |
| --- | --- |
| Cover switch (`X`) of type momentary with a stop button | hold stop for 5 s (touch chips that report one pad at a time cannot hold two) |
| Cover switch (`X`) of type momentary without a stop button | hold open and close together for 3 s; a short press of both still stops the cover |
| Two or more switch buttons (`S`), the first two of type `momentary` or `momentary_nc` | hold the first two switch buttons of the config together for 3 s |
| One switch button (`S`, e.g. an outlet) of type `momentary` or `momentary_nc` | hold it for 10 s |

Inputs of type toggle (rocker or latching switches) have no gesture: their
contact stays closed while the load is on. Changing the switch type takes
effect on the gesture at once. Onboard reset buttons (`B`) have no gesture:
holding one for 2 s is the factory reset. The presses of a locking gesture act
as usual (with the default momentary settings a 3 s or 10 s hold is a long
press, so no relay toggles); the presses of an unlocking gesture are ignored.

The lock LED blinks 3 times when the lock engages, twice when it is released
and once for a press while locked (250 ms on, 250 ms off); otherwise it shows
nothing for the lock. A write over Zigbee does not blink it. The lock LED is
the `P` LED of the config string, else the status LED (`L`), else the first
indicator (`I`). When the `P` pin is also an `L` or `I` LED, or the fallback
is used, the blinks are layered over that LED and it returns to its previous
state afterwards. While that LED blinks for the network (not joined) it shows
no lock blinks; the lock itself still changes.
