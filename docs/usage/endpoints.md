# Zigbee endpoints

> Read this to bind the device directly to other devices, add it to a group or
> address its clusters from your own tools. The settings themselves are
> described in [configuration.md](./configuration.md).

Each function of a multi-gang device lives on its own Zigbee endpoint. The
[config string](./config_string.md) decides the endpoints, always in this
order:

1. **switch** endpoints, one per switch button (`S`),
2. **relay** endpoints, one per relay (`R`),
3. **cover-switch** endpoints, one per cover switch (`X`),
4. **cover** endpoints, one per cover (`C`).

Numbering starts at 1 and has no gaps. Examples:

| Device | Config tokens | Endpoints |
| --- | --- | --- |
| 4-gang switch | 4 × `S`, 4 × `R` | 1–4 switches, 5–8 relays |
| Outlet with one button | `S`, `R` | 1 switch, 2 relay |
| Relay module without inputs | 2 × `R` | 1–2 relays |
| Battery remote, 4 buttons | 4 × `S` | 1–4 switches, no relays |
| Shutter switch | `X`, `C` | 1 cover switch, 2 cover |

A device has at most 8 endpoints.

## Endpoint 1

Endpoint 1 always carries the device clusters, next to whatever endpoint 1 is
in the list above:

| Cluster | Id | Present |
| --- | --- | --- |
| Basic | `0x0000` | always |
| Device Config | `0xFC10` | always |
| Device Diagnostics | `0xFC11` | always |
| OTA Upgrade (client) | `0x0019` | always |
| Power Configuration | `0x0001` | battery devices (`BT` token) |
| Poll Control | `0x0020` | end-device images |
| Electrical Measurement, Metering, Meter Calibration | `0x0B04`, `0x0702`, `0xFC12` | devices with a meter (`E` token) |

## Clusters per endpoint kind

| Endpoint | Server clusters | Client clusters (send to bindings) |
| --- | --- | --- |
| Switch | On/Off Switch Configuration `0x0007`, Multistate Input `0x0012`, Button Event `0xFC02` | On/Off `0x0006`, Level Control `0x0008` |
| Relay | On/Off `0x0006`, Level Control `0x0008`, Groups `0x0004` | — |
| Cover switch | Cover Switch Config `0xFC01`, Multistate Input `0x0012`, Button Event `0xFC02` | Window Covering `0x0102` |
| Cover | Window Covering `0x0102` | — |

A relay's Level Control accepts *Move to Level with On/Off* only: level 0
switches the relay off, any other level on. Its On/Off accepts On, Off,
Toggle, *On With Timed Off*, *Off With Effect* (as Off) and *On With Recall
Global Scene* (as On).

## Binding

A switch endpoint sends On/Off commands, and Level Control commands while a
button is held, to the devices bound to it. A cover-switch endpoint sends
Window Covering commands. When these commands go out is set by the binded
mode ([configuration.md](./configuration.md#binded-mode-and-long-press-dimming)).

To let the second button of a 2-gang module control a bulb, bind endpoint 2
of the module to the bulb's endpoint with the `OnOff` cluster (add
`LevelCtrl` for dimming):

![bulb binding](/docs/.images/bind_bulb.png)

Bind a relay endpoint's On/Off server from another switch to control the relay
directly.

## Groups

Relay endpoints accept group membership. To switch the first relay of two
2-gang modules together, add endpoint 3 of both to the same group:

![add to group](/docs/.images/add_to_group.png)

End-device images do not receive group commands; see
[known_issues.md](../known_issues.md).

## Fork-private clusters

Every fork-private cluster answers only requests sent with the manufacturer
code `0x1A2B` (6699, defined in `helper_scripts/management_protocol.json`).
The generated Zigbee2MQTT, ZHA and HOMEd integrations send it.

| Cluster | Id | Endpoint |
| --- | --- | --- |
| Cover Switch Config | `0xFC01` | cover-switch endpoints |
| Button Event | `0xFC02` | switch and cover-switch endpoints |
| Device Config | `0xFC10` | 1 |
| Device Diagnostics | `0xFC11` | 1 |
| Meter Calibration | `0xFC12` | 1, with a meter |

### Device Config (`0xFC10`)

| Attribute | Id | Type | Access |
| --- | --- | --- | --- |
| SchemaVersion | `0x0000` | uint16 | read |
| Revision | `0x0001` | uint32, counts accepted config writes | read |
| Text | `0x0002` | long string, the [config string](./config_string.md) | read, write |
| Status | `0x0003` | enum8: 0 ready, 1 reboot pending, 2 error | read |
| LastError | `0x0004` | enum8: 0 none, 1 argument, 2 parse, 3 resource, 4 nvm, 5 pending, 6 unsupported, 7 overflow, 8 feature missing | read |
| StatusLedMode | `0x0010` | enum8: 0 network, 1 off, 2 on | read, write |
| MultiPressReset | `0x0011` | uint8, 0–10 (0 off) | read, write |
| ChildLock | `0x0012` | boolean | read, write |

A stored config the firmware cannot run boots the recovery graph: endpoint 1
with Basic, Device Config, Device Diagnostics and OTA only (see
[config_string.md](./config_string.md#when-a-config-string-is-rejected)).
Every general-purpose pin that is not driven as an output then rests as an
input with the pull-down (TLSR8258: ports A–D without `PA7`/SWS, the debug UART
`PB1` in `DEBUG=1` builds and the 32 kHz crystal `PC2`/`PC3` when fitted;
EFR32: ports A–D without SWD `PA1`/`PA2`, the debug USART `PA5`/`PA6` when
present and the LFXO `PD0`/`PD1` when used), so relay driver inputs do not
float and chatter on mains hum.

### Child lock

`ChildLock` (`0x0012`) locks the local buttons. No reporting is configured for
it; a lock toggled on the device sends one Report Attributes command, with the
manufacturer code, to the coordinator. Behaviour and gestures:
[configuration.md](./configuration.md#child-lock).

### Device Diagnostics (`0xFC11`)

Read-only `uint32` counters, refreshed once per second. All except uptime are
reportable.

| Attribute | Id | Counts |
| --- | --- | --- |
| UptimeMs | `0x0000` | ms since boot |
| Capabilities | `0x0001` | bitmap32 of image capabilities |
| GpioEdges, GpioDropped, GpioRearmHits | `0x0010`–`0x0012` | input edges captured, edges lost to a full queue, interrupt re-arm limit hits |
| ButtonEvents, Gestures | `0x0020`, `0x0021` | debounced press and release events, recognised gestures |
| ZbSubmitted, ZbDropped, ZbExpired, ZbSendFailed, ZbHighWater | `0x0030`–`0x0034` | button events sent, dropped on a full queue, expired after 5 s, failed sends, highest queue fill |
| NetworkTransitions, NetworkLosses, NetworkJoins | `0x0040`–`0x0042` | network state changes, losses, joins |
| Announces, AnnounceFailures, Steering | `0x0043`–`0x0045` | device announcements, failed announcements, network steering attempts |
| DescriptorFailures, LastDescriptorError | `0x0050`, `0x0051` | Zigbee graph validation failures, last failure reason (enum8) |

### Button Event (`0xFC02`)

| Attribute | Id | Type | Access |
| --- | --- | --- | --- |
| ButtonState | `0x0000` | enum8: 0 released, 1 pressed | read, report |
| LastEventSeq | `0x0001` | uint16 | read, report |
| MultiClickGap | `0x0002` | uint16 ms, 100–2000 | read, write |
| DebounceMs | `0x0003` | uint16 ms, 1–200 | read, write |

The cluster sends command `0x00` *ButtonEvent* (server to client) to the
coordinator: `seq` uint16, `press_id` uint16, `event_type` uint8 (0 press,
1 release, 2 hold, 3 hold release, 4 clicks), `count` uint8 (clicks),
`duration_ms` uint16 (hold release). See [button_events.md](./button_events.md).

### Cover Switch Config (`0xFC01`)

| Attribute | Id | Type | Values |
| --- | --- | --- | --- |
| SwitchType | `0x0000` | enum8 | 0 toggle, 1 momentary |
| CoverIndex | `0x0001` | uint8 | 0 detached, n = n-th cover |
| Reversal | `0x0002` | boolean | swap open and close inputs |
| LocalMode | `0x0003` | enum8 | 0 immediate, 1 short press, 2 long press, 3 hybrid |
| BindedMode | `0x0004` | enum8 | as LocalMode |
| LongPressDuration | `0x0005` | uint16 | ms |

### Meter Calibration (`0xFC12`)

| Attribute | Id | Type | Values |
| --- | --- | --- | --- |
| VoltageGainPpm | `0x0000` | uint32 | 500000–1500000 (1000000 = 100 %) |
| CurrentGainPpm | `0x0001` | uint32 | as above |
| PowerGainPpm | `0x0002` | uint32 | as above; also scales energy |

Command `0x00` *ResetEnergy* (no payload) sets the energy counter to 0.

## Manufacturer-specific attributes on standard clusters

These attributes carry no manufacturer code.

### On/Off Switch Configuration (`0x0007`, switch endpoints)

| Attribute | Id | Type | Values |
| --- | --- | --- | --- |
| SwitchType (ZCL) | `0x0000` | enum8 | read-only copy of the mode below |
| SwitchActions (ZCL) | `0x0010` | enum8 | 0 on_off, 1 off_on, 2 toggle_simple, 3 toggle_smart_sync, 4 toggle_smart_opposite |
| Mode | `0xFF00` | enum8 | 0 toggle, 1 momentary, 2 momentary_nc |
| RelayMode | `0xFF01` | enum8 | 0 detached, 1 press_start, 2 long_press, 3 short_press, 4 deferred_single |
| RelayIndex | `0xFF02` | uint8 | 1–n relay endpoint; 0 on devices without relays |
| LongPressDuration | `0xFF03` | uint16 | ms; 0 = no long press |
| LevelMoveRate | `0xFF04` | uint8 | level units per second |
| BindedMode | `0xFF05` | enum8 | 1 press_start, 2 long_press, 3 short_press |

### On/Off (`0x0006`, relay endpoints)

| Attribute | Id | Type | Values |
| --- | --- | --- | --- |
| OnOff | `0x0000` | boolean | read, report |
| StartUpOnOff | `0x4003` | enum8 | 0 off, 1 on, 2 toggle, `0xFF` previous |
| IndicatorMode | `0xFF01` | enum8 | 0 same, 1 opposite, 2 manual (relays with an LED) |
| IndicatorState | `0xFF02` | boolean | LED state (relays with an LED) |
| InchingDuration | `0xFF03` | uint16 | ms; 0 = off |
| InterlockGroup | `0xFF04` | uint8 | 0 = none, or a `K` group number |

### Window Covering (`0x0102`, cover endpoints)

`CurrentPositionLiftPercentage` (`0x0008`) follows ZCL: 0 = fully open,
100 = fully closed, `0xFF` while the position is unknown. Zigbee2MQTT, ZHA and
HOMEd show 100 = open. ZHA shows an unknown position as no value, Zigbee2MQTT
keeps the last position, HOMEd shows an out-of-range position.

| Attribute | Id | Type | Values |
| --- | --- | --- | --- |
| Moving | `0xFF00` | enum8 | 0 stopped, 1 opening, 2 closing (read, report) |
| MotorReversal | `0xFF01` | boolean | swap the motor relays |
| OpenTime | `0xFF02` | uint32 | ms, 0–429496 |
| CloseTime | `0xFF03` | uint32 | ms, 0–429496 |
| ClosedDeadzone | `0xFF04` | uint16 | 1/100 %, closed and open deadzone together below 100 % |
| OpenDeadzone | `0xFF05` | uint16 | 1/100 % |
| Overrun | `0xFF06` | uint16 | ms added to a move to an end |

Commands: Up/Open, Down/Close, Stop, Go To Lift Percentage. A Go To Lift
Percentage is refused with `ACTION_DENIED` while the cover has no travel time
or, for an intermediate position, while the position is unknown. Behaviour:
[configuration.md](./configuration.md#how-a-cover-moves).
