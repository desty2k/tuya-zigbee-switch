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

`MultiClickGap` (100–2000 ms, default 350 ms) and `DebounceMs` (1–200 ms, default 8 ms) are writable on `0xFC02` per endpoint. A cover-switch endpoint applies its values to both its open and close inputs. HOMEd exposes these settings and `ButtonState` as custom attributes; its extension format does not represent custom cluster commands, so command actions are available through Zigbee2MQTT and ZHA.

The Basic cluster (`0x0000`) exposes read-only `uint32` diagnostic counters: GPIO edges captured and dropped, logical button events and gestures emitted, queued Zigbee button events dropped, and GPIO rearm-limit hits. They are useful for diagnosing missed input or noisy wiring without a debug probe.
