# Zigbee Endpoints in Firmware

> Read this document if you want to directly bind the device or add it to a group.

As the firmware supports multi-channel (multi-gang) devices, it uses Zigbee endpoints to handle command routing. Zigbee endpoints are numbered, starting from one. For each endpoint, only one instance of a specific function can exist. For example, there can only be a single relay (`OnOffCluster`) attached to endpoint 1. This document explains how the firmware assigns and uses endpoints.

If the device is an N-gang switch module, the firmware will use `2 × N` endpoints. The first N endpoints are used for "client" (output) OnOff clusters, which can control other Zigbee devices via direct bindings. The next N endpoints (endpoints N+1 to 2×N) are used for "server" (input) OnOff clusters, which are directly linked to physical relays.

Here is an example table:

| Endpoint | Clusters     | Description                                                                                  |
|----------|--------------|----------------------------------------------------------------------------------------------|
| 1        | OnOff client | Binding to control other Zigbee devices                                                      |
| ...      | OnOff client | ...                                                                                          |
| N        | OnOff client | Binding to control other Zigbee devices                                                      |
| N+1      | OnOff server | Controls Relay 1 state. Add to a group or bind it with another device to control the relay.  |
| ...      | OnOff server | ...                                                                                          |
| 2N       | OnOff server | Controls Relay N state. Add to a group or bind it with another device to control the relay.  |

## Usage Examples

### Controlling a Smart Bulb

If you have a 2-gang module and want its second button to control a smart bulb via Zigbee direct communication:

Bind endpoint 2 of your device to endpoint 1 of the bulb, and bind the `OnOff` cluster, as shown in the screenshot:

![bulb binding](/docs/.images/bind_bulb.png)

### Creating a Zigbee Group

If you have two 2-gang devices and want to group the first relay of both devices, you should add endpoint 3 of both devices to the same group, as shown in the screenshot:

![add to group](/docs/.images/add_to_group.png)

## Button events and diagnostics

Each switch and cover-switch endpoint also has the Button Event cluster (`0xFC02`). It sends immutable actions to the coordinator while keeping the physical button state in a separate `ButtonState` attribute. The event payload contains a per-device sequence number and press identifier so a coordinator can identify missed events and associate press, release, and hold events.

Zigbee2MQTT exposes `press`, `release`, `hold`, and `hold_release` actions, plus `single`, `double`, `triple`, `quadruple`, or `multi_click` with `action_count` for longer click sequences. `action_duration` is reported for `hold_release`; `action_seq` is the firmware event sequence. The generated ZHA quirk provides equivalent button automations and exposes the button state as a disabled-by-default diagnostic entity.

`MultiClickGap` (100–2000 ms, default 350 ms) and `DebounceMs` (1–200 ms, default 25 ms) are writable on `0xFC02` per endpoint. A cover-switch endpoint applies its values to both its open and close inputs. HOMEd exposes these settings and `ButtonState` as custom attributes; its extension format does not represent custom cluster commands, so command actions are available through Zigbee2MQTT and ZHA.

## Device management

Endpoint 1 carries two manufacturer-specific clusters. Every fork-private cluster (`0xFC01`, `0xFC02`, `0xFC10`, `0xFC11`) answers only requests sent with the manufacturer code `0x1A2B` (`helper_scripts/management_protocol.json`); the generated Zigbee2MQTT, ZHA and HOMEd integrations send it.

- **Device Config** (`0xFC10`): the config string (`Text`, writable), its `Status` and `LastError`, the status LED mode (`network`, `off`, `on`), the multi-press reset count (0 disables it) and the child lock (below).
  A stored config the firmware cannot run (malformed, over the platform's limits, or with a token for a feature the image lacks: `LastError` `FEATURE_MISSING`) boots the recovery graph: endpoint 1 with Basic, Device Config, Device Diagnostics and OTA only, so a corrected config or another image can still be installed. Every general-purpose pin that is not driven as an output then rests as an input with the pull-down (TLSR8258: ports A–D without `PA7`/SWS and the debug UART `PB1` in `DEBUG=1` builds and the 32 kHz crystal `PC2`/`PC3` when fitted; EFR32: ports A–D without SWD `PA1`/`PA2`, the debug USART `PA5`/`PA6` when present and the LFXO `PD0`/`PD1` when used), so the relays of the rejected config stay off instead of floating and chattering on mains hum.
- **Device Diagnostics** (`0xFC11`): read-only `uint32` counters for uptime, GPIO edges captured and dropped, rearm-limit hits, button events and gestures, queued Zigbee button events (submitted, dropped, expired, send failures, high water), network transitions, losses, joins, announces and steering, and descriptor validation failures. The values refresh once per second; every counter except uptime is reportable. They are useful for diagnosing missed input or noisy wiring without a debug probe.

## Child lock

`ChildLock` (`0x0012`, boolean, writable) on Device Config makes the device ignore its local switch and cover-switch buttons, including a cover's stop button: a press that starts while locked does not switch a relay, move a cover or send a command to bindings. Zigbee commands (On/Off, Window Covering) and attribute writes keep working, and the lock survives reboots, power cuts and OTA updates. Button Event (`0xFC02`) reports keep reaching the coordinator.

A factory reset releases the lock: the multi-press reset still counts presses while locked, and an onboard button's 2 s hold works as before, so a locked device can always be reset and re-paired.

The integrations read the lock when the device is configured and on request (Zigbee2MQTT `get`); they configure no report for it, because the Telink reporting table has no slot left on 4-gang boards. A lock toggled on the device shows in Zigbee2MQTT, ZHA or HOMEd after the next read.

The lock is also toggled on the device:

| Inputs | Gesture |
| --- | --- |
| Cover switch (`X`) of type momentary | hold open and close together for 3 s; a short press of both still stops the cover |
| Two or more switch buttons (`S`), the first two momentary | hold the first two switch buttons of the config together for 3 s |
| One switch button (`S`, e.g. an outlet), momentary | hold it for 10 s |

Inputs of type toggle (rocker or latching switches) have no gesture: their contact stays closed while the load is on. Changing the switch type takes effect on the gesture at once. Onboard reset buttons (`B`) have no gesture: holding one for 2 s is the factory reset. The presses of a locking gesture act as usual (with the default momentary settings a 3 s or 10 s hold is a long press, so no relay toggles); the presses of an unlocking gesture are ignored. A gesture toggle is reported; a remote write is not echoed on the LED.

The lock LED blinks 3 times when the lock engages, twice when it is released and once for a press while locked (250 ms on, 250 ms off); otherwise it shows nothing for the lock. It is the `P` LED of the config string, else the status LED (`L`), else the first indicator (`I`). When the `P` pin is also an `L` or `I` LED, or the fallback is used, the blinks are layered over that LED and it returns to its previous state afterwards. While that LED blinks for the network (not joined, or the commissioning confirmation) it shows no lock blinks; the lock itself still changes.

## Covers

A cover endpoint reports `CurrentPositionLiftPercentage` the ZCL way: 0 = fully open, 100 = fully closed, `0xFF` while the position is unknown. Zigbee2MQTT, ZHA and HOMEd show 100 = open. ZHA shows an unknown position as no value; Zigbee2MQTT ignores it and keeps showing the last position; HOMEd cannot filter it and shows an out-of-range position. The Window Covering cluster also carries writable settings without a manufacturer code: `OpenTime` (`0xFF02`) and `CloseTime` (`0xFF03`), `uint32` in ms, 0–429496; `ClosedDeadzone` (`0xFF04`) and `OpenDeadzone` (`0xFF05`), `uint16` in 1/100 %, the travel next to each end that reports as fully closed or open, together below 100 %; `Overrun` (`0xFF06`), `uint16` ms added to a move to an end. The cover-switch cluster (`0xFC01`) holds the cover-switch long-press duration (`0x0005`, ms).
