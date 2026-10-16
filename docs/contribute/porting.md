*Open the **Outline** (table of contents) from the top right.*  

# Porting  

*Follow this guide if [supported_devices.md](/docs/supported_devices.md) does not include your device.* 

### Steps
1. Check compatibility
2. **Obtain the board pinout**
3. Add an entry to [`device_db.yaml`](../../device_db.yaml)
4. Build and install the firmware

## Compatibility 

[ZT series]: https://developer.tuya.com/en/docs/iot/zt-series-module?id=Kaiuym8ctid7k
[ZS series]: https://developer.tuya.com/en/docs/iot/zs-series-module?id=Kaiuyljrfi0wv
[TYZS series]: https://developer.tuya.com/en/docs/iot/tyzs-series-module?id=Kaiuylr3s96a6
[updating.md]: /docs/updating.md
[flashing/]: ../flashing/
[IEEE Address]: /docs/.images/screen_telink_mac.png
[TLSR825x]: https://wiki.telink-semi.cn/wiki/chip-series/TLSR825x-Series/

The firmware works on **Telink** ([TLSR825x]) and **Silabs** (EFR32MGxx) microcontrollers.  
⤷ Check the **[IEEE Address]** or open the device to identify the MCU.  

|                   | Telink                                | Silabs                                                |
|------------------:|---------------------------------------|-------------------------------------------------------|
| Devices           | • Most Tuya devices after 2023        | • Some Tuya remotes, switches <br> • All SONOFF, IKEA |
| Tuya modules      | [ZT series]                           | [ZS series], [TYZS series]                            |
| IEEE Address      | `0xa4c138xxxxxxxxxx`                  | Use *MAC lookup* website                              |
| Stock ➡ Custom FW | OTA [updating.md] or wire [flashing/] | wire [flashing/]                                      |

## Pinout

Every device has a different GPIO mapping.  
⤷ You must find **which pins the peripherals are connected to.**  

Example: button on D2, LED on C2, switch on B5, relay on C4.  

### Diagrams

**See [`diagrams/`](../diagrams/) for some simplifications and improvements over official datasheets.**

[labels]: https://github.com/romasku/tuya-zigbee-switch/issues/145#issuecomment-3303035527
[visible traces]: https://github.com/romasku/tuya-zigbee-switch/issues/146#issuecomment-3302750944
[lamp trick]: https://github.com/romasku/tuya-zigbee-switch/pull/188#issuecomment-3506916760
[solder points]: https://github.com/romasku/tuya-zigbee-switch/issues/183#issuecomment-3491147138
[follow pattern]: https://github.com/romasku/tuya-zigbee-switch/issues/181#:~:text=Pictures-,Configs,-We%20obtained%20the
[Tasmota]: https://templates.blakadder.com/switch.html
[ESPHome]: https://devices.esphome.io/type/switch
[OpenBeken]: https://openbekeniot.github.io/webapp/devicesList.html
[Tuya WiFi modules]: https://developer.tuya.com/en/docs/iot/wifi-module?id=Kaiuyi301kmk4

### Obtaining

There are multiple safe ways to obtain the pinout:
- **Look for clues on the PCB**: [labels], [solder points] and [visible traces] ([lamp trick]); examples from upstream history (romasku/tuya-zigbee-switch issues) 
- **Test continuity** (resistance) with a multimeter
- **Try each pin** until something works (edit *config string* in Z2M)
- Extract pinout from original firmware (memory dump)
- Truncate the pinout of a higher-gang model ([follow pattern], upstream history)
- Get pinout from WiFi variant ([Tasmota], [ESPHome], [OpenBeken] devices ↔ [Tuya WiFi modules])
- Ask someone else to do it 🙂

> [!CAUTION]  
> Tuya devices do not have galvanic isolation! *The DC circuit may operate at 230-235V.*  
> ***Do not plug a dissasembled device into mains power!*** 

### Config string

The pinout is stored in the **device config string**.
- Prepare it for the database entry  
- Update it in Z2M after flashing  
(try different pins, until the device works properly)

**Format**:  
⤷ `<new manufacturer>;<new model>;<pin setup 1>;<pin setup 2>;...;<pin setup n>;`  

**Simple example** (1-gang module with LED on `A2`, switch on `A3` and relay on `A4`):  
⤷ `ljasd9as;TS0001-ABC;LA2;SA3u;RA4;`  

**Complex example** (2-gang switch with bi-stable relays):  
⤷ `osap2dsa;TS0002-ABC;BC3u;LC2i;SB5u;RD2D4;IA0;SB4u;RD3B1;IA1;M;i43533;`

**Cover example** (1-gang cover controller):  
⤷ `mfgname;TS130F-CVR;BC5u;LA3;XA2B3u;CC4D2;`

**Cover with stop button and LEDs** (BSEED TS130F: open/close/stop buttons, open/close LEDs, red child lock LED on `D2`):  
⤷ `vsj5o7dm;TS130F-BS-CVR;LB4;XB6A1A0u;CD3C0;IC3;IB5;PD2;M;`

**Outlet with the child lock on its indicator** (BSEED TS011F: the relay indicator `C4` also blinks the child lock; hold the button 10 s to toggle it):  
⤷ `drzlxjne;TS011F-BS-PM-3;SB4u;RB5;IC4;PC4;EBC2C3D2;M;`

| Ch      | Peripheral    | Function                                                                                                          |
|--------:|---------------|-------------------------------------------------------------------------------------------------------------------|
| **`B`** | Reset button  | • Puts device in pairing                                                                                          |
| **`L`** | Network led   | • Blinks while pairing <br> • Is the backlight sometimes                                                          |
| **`S`** | Switch        | • User input <br> • Tactile/touch button or external switch <br> • Spam to put in pairing mode                    |
| **`R`** | Relay / Triac | • Output <br> • Non-latching: `RC1` - 1 pin: on when high <br> • Latching: `RC2C3` - 2 pins: pulse on, pulse off  |
| **`X`** | Cover Switch  | • User input for cover control <br> • Format: `XA2B3u` - 2 pins + pull resistor: open button, close button <br> • `XA2B3A4u` - optional third pin: stop button <br> • Holding stop for 5 s (or open and close together for 3 s without a stop button) toggles the child lock (momentary type) |
| **`C`** | Cover         | • Motor control for curtains/blinds/shades <br> • Format: `CA2B3` - 2 pins: open relay, close relay               |
| **`I`** | Indicator LED | • 1 per relay, follows state <br> • A cover takes the next 2: lit while opening, lit while closing <br> • Briefly flashes on button press (binding confirmation) <br> • Blinks while pairing if there is no network led |
| **`P`** | Child lock LED | • At most one <br> • Blinks 3 times on lock, twice on unlock, once for a press while locked; dark otherwise <br> • May name the pin of an `L` or `I` LED: the blinks are layered over it <br> • Without `P` the `L` LED is used, else the first `I` LED (see [child lock](../usage/endpoints.md#child-lock)) |

For buttons (`B`), switches (`S`), and cover switches (`X`), the character after the pin(s) chooses the internal pull resistor:  
⤷ **`u`: pull-up** (Telink 10 kΩ), `U`: pull-up (Telink 1 MΩ; Silabs has one pull-up strength, so it equals `u`), `d`: pull-down (Telink 100 kΩ), `f` or nothing: no pull (external resistor)  

Usually, pressing the button bridges the GPIO pin to Ground (active low).  
⤷ So we need a pull-up resistor `u`: to hold it at VCC (high) while not-pressed.

For LEDs, add `i` to invert the state.

Additional options: 
| Format       | Option                       | Function                                                                          |
|-------------:|------------------------------|-----------------------------------------------------------------------------------|
| **`D<N>`**   | Debounce delay               | • Set software debounce in ms for all buttons/switches; default 25 <br> • Must be 1–200; other values make the config invalid |
| **`G<N>`**   | Multi-click gap              | • Default multi-click gap in ms for all buttons: a click that follows within it counts towards the same multi-click; default 350 <br> • The parser accepts 0–65535; the `MultiClickGap` attribute (`0xFC02`) accepts 100–2000, so stay in that range |
| **`K<hex>[:<ms>]`** | Relay interlock group | • Needs the `interlock` feature <br> • `<hex>`: bit mask of relay indices (bit 0 = first relay; relays count in config order, a `C` cover takes two) <br> • `:<ms>`: dead time between one relay of the group switching off and another switching on, 0–65535 ms, default 200; `:0` switches over at once <br> • Switching one relay on switches the others in the group off <br> • Up to 10 groups; a mask naming a relay that does not exist makes the config invalid; mask 0 is ignored <br> • Each cover's two motor relays are interlocked without a `K` token <br> • Example: `RA0;RA1;K03;` |
| **`i00000`** | Image type                   | • Change OTA image_type (migrate to another build)                                |
| **`M`**      | Momentary                    | • Defaults buttons to momentary mode (for devices with built-in switches)         |
| **`BT<pin>`** | Battery mode                | • Enables battery-powered behavior <br> • Adds battery measurement/reporting using the selected ADC pin |
| **`SLP`**    | Simultaneous Latching Pulses |  • Enable simultaneous pulses for latching relays (they are disallowed by default)|
| **`EB<CF><CF1><SEL>`** | BL0937 energy meter | • Needs the `meter` feature (Telink: 2 hardware counters; not with `probe`) <br> • `CF` active power pulses, `CF1` voltage/current pulses, `SEL` output choosing CF1's quantity <br> • SEL high selects voltage; append `i` when SEL low selects voltage, e.g. `EBC2C3D2i` <br> • Meter n reports Electrical Measurement, Metering and Meter Calibration on endpoint n |

## GPIO probe (devices that cannot be opened)

When a device cannot be opened, its pins are found with the GPIO probe image
running on the device itself. A probe board in `device_db.yaml` has `probe` in
its `features`, optionally with `relay` and `switch` (no other feature; `interlock` is added for it), its
own `firmware_image_type`, and the stock `stock_manufacturer_id` /
`stock_image_type` read from that exact device. Its config string holds the
manufacturer and model fields and the pins found so far; every pin the config
claims is driven by its feature and left out of the probe.
Example: `OUTLET_BSEED_TS011F_PROBE` for TS011F `_TZ3210_drzlxjne`, with
`features: [probe, relay, switch]` and the button, relay and relay indicator
found on hardware, with the button momentary (`SB4u;RB5;IC4;M;`).

With `relay` and `switch` in the image, a candidate config string is tested
on the device without reflashing: write it to the Device Config `text`
attribute (cluster `0xFC10`), the device reboots into it, its buttons and
relays work through the usual endpoints, and the probe keeps working on every
pin the config does not claim. A config with a token for a feature the image
lacks boots the recovery graph, which disables the probe and rests every
pin on the pull-down, as in every image; write a config
without that token to get it back. When probing a cover device, configure its
two motor relays as plain relays in one interlock group (`RA0;RA1;K03;`, with
the default 200 ms dead time) so the two directions are never energised
together.

`make tools/check_device_features` enforces the probe rules (`probe` with at
most `relay` and `switch`, Telink router). The board has `build: no`, and a
probe image is never
added to the repository OTA indexes (`zigbee2mqtt/ota/`): Zigbee2MQTT installs
read those indexes, and the probe's `from_tuya` entry would be offered to every
stock device with that manufacturer name. `make board/build` and
`make board/update-indexes` refuse probe boards, and
`helper_scripts/make_z2m_ota_index.py` refuses to write one into
`zigbee2mqtt/ota/`.

Build the images only, then install from a local index that contains nothing
but the probe entry:

```bash
BOARD=OUTLET_BSEED_TS011F_PROBE make board/build-firmware board/generate-ota-files
python3 helper_scripts/make_z2m_ota_index.py --db_file device_db.yaml \
    --board OUTLET_BSEED_TS011F_PROBE --base-url "$PWD" \
    bin/router/OUTLET_BSEED_TS011F_PROBE/tlc_switch-<version>-from_tuya.zigbee \
    /path/outside/repo/probe_index.json
```

Point Zigbee2MQTT's `ota.zigbee_ota_override_index_location` at that file
([updating.md]) only for the probe session, update the one expendable device,
then restore the previous index. The `url` is an absolute path, so the `bin/`
directory must be readable by Zigbee2MQTT (copy the image and index to its
data directory otherwise).

> [!WARNING]
> Conversion from stock is one-way: returning to stock or recovering a bad
> image needs the SWS pad, which needs opening. Use an expendable device. The
> outlet board is not isolated from mains: probe only over Zigbee, with the
> device closed and **nothing plugged in** while pins are driven.

Before converting, record the stock identity from Zigbee2MQTT: `genBasic`
`appVersion`, `stackVersion`, `hwVersion`, `dateCode`, `swBuildId`, and from
the OTA "check for update" log `manufacturerCode`, `imageType`, `fileVersion`.
The probe board's stock ids come from these values, never from a sibling.
The OTA index generator refuses an entry with a stock image type and no
`manufacturerName` filter; the probe's entries are filtered to the board's
stock manufacturer name.

What the probe does (commands on cluster `0xFC11`, fork manufacturer code,
defined in `helper_scripts/management_protocol.json`):

| Step | Effect |
| --- | --- |
| Boot | Every probeable pin becomes an input with the 100 kΩ pull-down (the rest state), so no relay driver input floats and half-energises its coil on mains hum; nothing is persisted. The recovery graph of every image rests the same pins the same way (`hal_gpio_rest_all()`) |
| Snapshot | Reads every probeable pin without changing its mode |
| Pull test | Pull-up (10 kΩ), pull-down (100 kΩ), twice, 25 ms each; classifies each pin *floating*, *driven high*, *driven low* or *active*; ends in the rest state. A relay driven through a transistor with a base resistor of about 20 kΩ or more classifies *floating* and may click during the pull-up phases; one with a low base resistor (a few kΩ) reads *driven low* |
| Watch | Counts rising edges on up to 2 pins for 1–300 s: count, min/max interval, last level; a pin above 20 kHz is stopped and flagged |
| Arm + pulse | Drives one *floating* pin to a level for 1–500 ms, or one *driven low* pin to level 1 for 1–50 ms, then the rest state with output latch 0. *Driven high* and *active* pins are never driven. A *driven low* pin may be a low-resistance transistor base or a chip output such as BL0937 `CF`, so it is only lifted briefly. Needs an arm before each pulse; the pin must show its class again under both pulls right before it is driven. The cutoff runs on the main loop (a task, re-checked every pass), not in an interrupt; if the main loop stalls, the 1 s watchdog reset is the backstop, so a pulse can last up to about 1 s |
| Stop | Ends every operation, returns its pins to input without pull |

Pins needed by the platform (TLSR8258 SWS `PA7`, flash `PE0`–`PE3`, debug UART
and 32 kHz crystal pins when used) and pins claimed by the config are never
probeable.

Procedure, with `helper_scripts/probe_discovery.py <friendly name>` driving
Zigbee2MQTT over MQTT (needs `paho-mqtt` from `requirements.txt` and the fork
converter `zigbee2mqtt/converters/switch_custom.js`, which carries the probe
board as model `TS011F-probe` although the board is `build: no`; it exposes the
endpoints of the board's config string and publishes the probe attributes
under the `probe` state key with octet strings in hex).
It reads one probe attribute per request, so no Read Attributes response needs
APS fragmentation:

1. OTA the probe image onto one expendable device with nothing plugged in.
2. Snapshot, then pull test all pins.
3. Watch the non-floating pins with no load: steady pulses are BL0937 `CF1`
   (mains voltage).
4. Watch while pressing the button: the pin with edges is the button.
5. Watch with a resistive load: a pin whose rate rises with the load is `CF`.
   No pulse activity at all suggests a BL0942 (UART).
6. Unplug the load. Pulse each floating pin high and low, and each driven-low
   pin with no pulse activity high for 50 ms, and answer what was heard (relay
   click) or seen (LED). A floating pin that changes the `CF1` rate while high
   is `SEL`.
7. The helper prints a proposed config string and meter pin map. Put them in
   the board's `device_db.yaml` entry (`config_str`, with the findings in
   `info`) and in the commit message, then write the real profile.

The probe image reports its own image type (47116), which no published index
entry matches. Leaving it means installing the real outlet firmware from a
local OTA index whose entry carries that image type.

## Build and install

[`device_db.yaml`]: /device_db.yaml
[device_db_explained.md]: ./device_db_explained.md
[building.md]: ./building.md

1. Add an entry to [`device_db.yaml`] following [device_db_explained.md];
   validate it with `device_db.schema.json` (e.g. the YAML VS Code extension)
   and run `make tools/check_device_features` and `make unit_tests` (the
   latter parses every built board's config string with the firmware parser).
2. Build the board: `BOARD=<key> make board/build-firmware board/generate-ota-files`
   ([building.md]).
3. Install it over the air from a local OTA index ([updating.md]) or by wire
   ([flashing/]), with the converters regenerated from your branch
   (`make tools/update_converters`).  
