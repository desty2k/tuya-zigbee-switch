# Button events

Every switch endpoint (`S` button) and cover-switch endpoint (`X` open and
close inputs) sends what the buttons do to the coordinator as button events.
Use them to run automations on single, double, triple and longer clicks or on
holds, with or without a relay attached.

Button events come from the fork-private Button Event cluster (`0xFC02`). They
go to the coordinator only, never to bindings. A press that starts while the
[child lock](./configuration.md#child-lock) is on sends no button events, so
automations on button actions are locked too.

## Gestures

| Event | When |
| --- | --- |
| press | the button goes down (after debounce) |
| release | the button goes up |
| hold | the button stays down for the long press duration |
| hold release | a held button is released; carries the hold's duration in ms |
| n clicks | the button was pressed and released n times (1–10), each release followed by the next press within the multi-click gap |

Rules:

- A click sequence ends when the multi-click gap passes without a new press.
  The tenth click ends it at once, so the longest sequence is 10.
- A press that becomes a hold is not a click. Clicks before it are sent first,
  then the hold.
- With the long press duration set to 0 there are no holds: every press is a
  click.
- For a rocker (`toggle` switch type), press is the contact closing and release
  the contact opening.
- A cover switch reports its open and close inputs on its endpoint; a stop
  button sends no button events.

## Timing

| Setting | Range | Default | Effect |
| --- | --- | --- | --- |
| Debounce (`<endpoint>_debounce`) | 1–200 ms | 25 ms, or `D` in the config string | how long the input must stay stable before press or release counts |
| Multi-click gap (`<endpoint>_multi_click_gap`) | 100–2000 ms | 350 ms, or `G` in the config string | longest pause after a release that still continues the click sequence; also the delay before a click sequence is reported |
| Long press duration (`<endpoint>_long_press_duration`) | 0–5000 ms | 800 ms | hold time for hold events, long-press relay and binded modes and dimming |

A cover-switch endpoint applies its debounce and gap to both its open and close
inputs.

## Zigbee2MQTT

Zigbee2MQTT publishes `action` with the event and the endpoint name, as in
`single_switch_left` or `hold_cover_switch`:

| Event | `action` |
| --- | --- |
| press | `press_<endpoint>` |
| release | `release_<endpoint>` |
| hold | `hold_<endpoint>` |
| hold release | `hold_release_<endpoint>`, with `action_duration` in ms |
| 1, 2, 3, 4 clicks | `single_<endpoint>`, `double_<endpoint>`, `triple_<endpoint>`, `quadruple_<endpoint>` |
| 5–10 clicks | `multi_click_<endpoint>`, with `action_count` |

Click actions also carry `action_count`. Every action carries `action_seq`, a
counter of the events the device sent: a gap in it means an event was lost.
Endpoint names are listed in [configuration.md](./configuration.md#names-in-the-integrations).

Example Home Assistant automation that runs on a double click of the second
button of a 4-gang switch (`switch_1`, counted from 0):

```yaml
triggers:
  - trigger: mqtt
    topic: zigbee2mqtt/<device friendly name>
    value_template: "{{ value_json.action }}"
    payload: double_switch_1
actions:
  - action: light.toggle
    target:
      entity_id: light.hallway
```

## ZHA

The quirk turns every button event into a `zha_event` with `command` set to
`press`, `release`, `hold`, `hold_release`, `single`, `double`, `triple`,
`quadruple` or `multi_click`, and the arguments `seq`, `press_id`, `count`
and `duration`. It also offers device triggers per endpoint:

| Device trigger | Subtype | Event |
| --- | --- | --- |
| short press | `button_<endpoint>` | single click |
| double, triple, quadruple press | `button_<endpoint>` | 2, 3, 4 clicks |
| long press | `button_<endpoint>` | hold |
| long release | `button_<endpoint>` | hold release |
| short press | `button_<endpoint>_raw` | press |
| short release | `button_<endpoint>_raw` | release |

`<endpoint>` is the endpoint number. Five or more clicks are available only as
`zha_event` with `command: multi_click`:

```yaml
triggers:
  - trigger: event
    event_type: zha_event
    event_data:
      device_ieee: <device IEEE address>
      endpoint_id: 2
      command: multi_click
      args:
        count: 5
```

The quirk also exposes the button state (`released`, `pressed`) as a
diagnostic entity that is disabled by default.

## HOMEd

HOMEd's extension format cannot describe custom cluster commands, so HOMEd
receives no button events. It exposes the button state (`buttonState`), the
multi-click gap and the debounce per endpoint, and the switch's `pressAction`
(`released`, `press`, `long_press`, `position_on`, `position_off`). Use
Zigbee2MQTT or ZHA for click automations.

## Relays and clicks

Button events are sent whatever the relay mode. To use clicks without the
relay switching on every one of them:

- `detached` relay mode: the button never switches the relay.
- `deferred_single` relay mode: only a single click switches the relay, after
  the multi-click gap; double and longer clicks and holds leave it alone.

The binded mode still applies: with the default `short_press`, every release
sends an On/Off command to bound devices, so a double click toggles a bound
light twice. Remove the bindings of a button used only for automations.

## Long press dimming

Holding a momentary button sends *Move with On/Off* to devices bound to the
switch endpoint's Level Control cluster, and the release sends *Stop with
On/Off*; the direction alternates with every hold. The speed is
`<switch>_level_move_rate` (level units per second, default 50, so a full
sweep of 254 steps takes about 5 s). The hold and hold release events are sent
as well. Details in [configuration.md](./configuration.md#binded-mode-and-long-press-dimming).

## Delivery

Events wait in a queue of 16 and are sent 40 ms apart, retried every 100 ms
on failure. An event that could not be sent within 5 s (for example while the
device is not joined) is dropped. The Device Diagnostics counters `zb_submitted`,
`zb_dropped`, `zb_expired`, `zb_send_failed` and `zb_high_water` count these
cases (see [faq.md](../faq.md#how-do-i-read-the-diagnostics-counters)).
