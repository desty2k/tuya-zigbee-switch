# Device config string

Each board has its own firmware image, but the image does not hard-code the
hardware layout: it comes from a text string stored on the device that names
the pins of buttons, relays, LEDs, covers and meters. The image carries its board's default
string, and the device uses it until you write another one.

Change it only to fix a wrong pinout or to adapt a board, for example to add
an interlock or a stop button. To port a new device, see
[contribute/porting.md](../contribute/porting.md).

## Format

```
<manufacturer>;<model>;<token>;<token>;…;
```

Example, the BSEED TS0726 4-gang switch:

```
pzao9ls1;BS4;LC1;SD3u;RA0;ID2;SD7u;RA1;IB4;SD4u;RC2;IB5;SB6u;RC3;IC4;M;
```

- The first two fields are the manufacturer and model names the device reports
  in its Basic cluster (each at most 31 characters). The Zigbee2MQTT, ZHA and
  HOMEd integrations recognise the device by them: keep them unchanged.
- Each token is at most 31 characters and ends with `;`.
- The whole string is at most 127 characters.
- A pin is a port letter and a number: `C2`, `B5`. Which pins exist depends on
  the chip (TLSR8258 or EFR32). The Zigbee2MQTT converter accepts pins `A0`–`D7`.

## Tokens

| Token | Meaning |
| --- | --- |
| `S<pin><pull>` | Switch button: a switch endpoint. The n-th `S` drives the n-th relay endpoint by default |
| `B<pin><pull>` | Onboard reset button: holding it for 2 s resets the device. No endpoint |
| `BT<pin>` | Battery voltage input of a battery device: adds Power Configuration on endpoint 1. May be a button pin |
| `R<pin>` | Relay: a relay endpoint. The pin is driven high to switch the relay on |
| `R<on pin><off pin>` | Latching (bistable) relay: a 100 ms pulse on the first pin switches it on, on the second pin off |
| `L<pin>[i]` | Status LED: shows the network state and the [status LED mode](./configuration.md#status-led-mode) |
| `I<pin>[i]` | Indicator LED: the n-th `I` belongs to the n-th relay and switch endpoint; covers take the next two each (lit while opening and while closing) |
| `P<pin>[i]` | Child lock LED (at most one). May name the pin of an `L` or `I` LED |
| `X<open><close>[<stop>]<pull>` | Cover switch: open and close buttons, optional stop button. A cover-switch endpoint |
| `C<open><close>` | Cover: the open and close motor relays. A cover endpoint |
| `EB<CF><CF1><SEL>[i]` | BL0937 power meter on endpoint 1 (Telink only). `i`: SEL low selects voltage |
| `K<hex mask>[:<ms>]` | Relay interlock group, dead time in ms (default 200) |
| `D<ms>` | Debounce of every button, 1–200 ms (default 25) |
| `G<ms>` | Multi-click gap of every button, 100–2000 ms (default 350) |
| `M` | Switch buttons start as `momentary` instead of `toggle` |
| `SLP` | Latching relays may pulse at the same time (by default one pulse at a time) |
| `i<number>` | OTA image type the device announces (decimal). Set by the board definition; do not change it |

`D`, `G` and `M` set the defaults that a switch or cover switch uses until its
own setting is changed; they apply to every button wherever they appear in the
string.

### Pull suffix

Buttons (`S`, `B`, `X`) take a pull suffix:

| Suffix | Input | Pressed when |
| --- | --- | --- |
| none or `f` | floating | the pin reads low |
| `u` | pull-up (10 kΩ on Telink) | the pin reads low |
| `U` | weak pull-up (1 MΩ on Telink; on Silabs the same as `u`) | the pin reads low |
| `d` | pull-down (100 kΩ on Telink) | the pin reads high |

The [switch type](./configuration.md#switch-type) `momentary_nc` inverts this
for a switch endpoint. The Zigbee2MQTT converter requires an explicit suffix on
`S` and `B`.

### `i` suffix on LEDs

An LED without `i` lights when its pin is high; with `i` it lights when the pin
is low.

### `K` — relay interlock

The mask is hexadecimal; bit 0 is the first relay of the config string, bit 1
the second, and so on. `R` tokens count as one relay, `C` tokens as two (open,
then close), in the order they appear. `K03` puts the first two relays in one
group; `K0C:500` puts the third and fourth in a group with a 500 ms dead time.

Groups are numbered 1, 2, … in the order of the `K` tokens; the relay setting
[`<relay>_interlock_group`](./configuration.md#interlock) uses these numbers.
A cover's two relays are always interlocked; they need no `K` token.

## Endpoints

The tokens decide the endpoints, always in this order:

1. one switch endpoint per `S`,
2. one relay endpoint per `R`,
3. one cover-switch endpoint per `X`,
4. one cover endpoint per `C`.

A string without any of these has one endpoint with the device clusters only.
See [endpoints.md](./endpoints.md).

## Limits

| What | Limit |
| --- | --- |
| Endpoints (`S` + `R` + `X` + `C`) | 8 |
| Switches (`S`) | 10 |
| Buttons (`S` + `B` + each `X` input) | 11 |
| Relays (`R` + 2 per `C`) | 10 |
| LEDs (`L` + `I`) | 5 |
| Covers (`C`) / cover switches (`X`) | 3 / 3 |
| Interlock groups (`K`) | 10 |
| Meters (`E`) | 1 on Telink (two counter inputs per meter, two available); none on Silabs |

The Zigbee tables also limit clusters (48) and attributes (160) across all
endpoints, so not every combination within these counts fits. Each pin may be
used once among buttons, relays and meter pins; LED pins may repeat.

## Changing the string

In Zigbee2MQTT, edit `device_config` (shown on the first endpoint, see
[configuration.md](./configuration.md#device-config)) and submit. The converter
checks the format first. The device then checks the string against what its
image can run:

- accepted: the device stores it, `device_config_status` shows
  `reboot_pending`, and the device restarts about 300 ms later with the new
  layout. Interview and reconfigure the device in Zigbee2MQTT when endpoints
  were added or removed.
- refused: the write fails and the device keeps its current string.

ZHA and HOMEd do not expose the string. Any tool that can write a
manufacturer-specific attribute can set it: cluster `0xFC10` (64528) on
endpoint 1, attribute `0x0002`, type long character string (`0x44`),
manufacturer code `0x1A2B` (6699). In the Zigbee2MQTT frontend this is the
device's Dev console.

Relay settings survive a new string as long as the relay endpoints and `K`
groups stay the same; pins, LEDs and buttons can change freely. A string that
changes them resets every relay setting to its default.

## When a config string is rejected

A stored string the image cannot run, for example after an OTA update to an
image without a feature the string uses, makes the device boot its **recovery
graph**:

- one endpoint with Basic, Device Config, Device Diagnostics and OTA only;
- Basic reports manufacturer `unknown` and model `recovery`, so the
  integrations show an unsupported device;
- every relay stays off and every free pin rests on its pull-down, so relay
  inputs do not float;
- `device_config_status` is `error` and `device_config_last_error` says why.

| `device_config_last_error` | Meaning | Fix |
| --- | --- | --- |
| `none` | no error | — |
| `parse` | the stored string is malformed, exceeds a limit, reuses a pin or references something that does not exist | write a corrected string |
| `resource` | the layout does not fit the Zigbee tables of this platform | write a string with fewer endpoints or features |
| `feature_missing` | a token needs a feature this image does not contain (`C`/`X` cover, `E` meter, `K` interlock, `BT` battery, `S` switch, `R` relay) | install the image built for this board, or write a string without that token |
| `nvm` | storing a setting failed | retry; a recurring error points to worn or full flash |
| `overflow` | the config revision counter (32 bits) is exhausted | flash the device by wire |

`argument`, `pending` and `unsupported` are part of the protocol but not
stored by the current firmware: a write refused for these reasons only fails.
A config write sent while the device waits to restart for the previous one is
refused; write again after the restart.

To leave the recovery graph, write a corrected string over the Dev console
(cluster, attribute and manufacturer code above), or install a matching image
over OTA (see [faq.md](../faq.md#the-device-shows-up-as-recovery)). The device
restarts with the new string and reports its own model again.

The board's default string is the `config_str` of its entry in
[`device_db.yaml`](../../device_db.yaml).
