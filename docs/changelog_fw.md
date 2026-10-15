_Open the **Outline** (table of contents) from the top right._

# Firmware Changelog

<!------------------------------------------------------

Please describe what you are working on, under ## Upcoming

------------------------------------------------------->

## Upcoming

- Telink: a cluster-specific command to a cluster without commands (power configuration, multistate input, on/off switch configuration, the fork-private clusters) is answered with an unsupported-command status; it reset the device.
- A switch whose relay index points at a relay endpoint drives that endpoint's relay when a cover is defined before the relays in the device config; it drove the cover's open relay.
- Telink: attribute reports from the fork-private clusters carry the fork's manufacturer code, as on Silabs.
- Telink: the `i<image_type>` config token sets the OTA image type again; it was parsed and ignored.
- Device Diagnostics (`0xFC11`) values refresh once per second instead of on every main-loop pass, and uptime is no longer reportable.

## v2.0.0-rc.2

- Fixes rc.1 resetting on relay changes, button actions, rejoins and most incoming ZCL commands other than Read Attributes: the Telink compiler miscompiled switch statements at `-Os`; all Telink objects now build without jump tables and the link fails if a malformed table dispatch appears.

## v2.0.0-rc.1

**Do not install.** Resets on relay changes and most incoming ZCL commands (fixed in rc.2).


First release of the BSEED-focused fork as its own product line. It does not
keep the settings of upstream or 1.x firmware: updating from them resets the
device's settings, while network membership, binds and groups survive.
Settings survive updates between 2.x releases. OTA file versions follow
`docs/contribute/release.md`, so every 2.x build is newer than any 1.x build.

### Devices

- **Hommyn 2-gang L-only relay module** (`_TZ3000_0e6uvexf`, `MODULE_HOMMYN_TS0012`)
  - Pinout: L1 `RB5`, L2 `RB4`, switches `SC2f`/`SC3f`
  - Defaults to momentary switch mode (`M;` in config)

### Features

- **GPIO probe image** (`features: [probe]`): discovers the pins of a device that cannot be opened, over Zigbee (cluster `0xFC11`); first board `OUTLET_BSEED_TS011F_PROBE` for TS011F `_TZ3210_drzlxjne`
- OTA index generator refuses entries with a stock image type and no `manufacturerName` filter
- **Button Event cluster** (`0xFC02`) sends press, release, hold, hold-release, and multi-click actions to the coordinator. Generated Zigbee2MQTT and ZHA integrations expose these actions, per-endpoint multi-click and debounce settings, and input diagnostics.
- **Cover cluster** (window covering) for controlling the motor of curtains, blinds, and shutters.
  Supports open, close, and stop commands with motor safety delays.
  - With open/close travel times set, the cover tracks its position and accepts *GoToLiftPercentage*.
    `CurrentPositionLiftPercentage` follows ZCL: 0 = fully open, 100 = fully closed, 0xFF = position unknown.
  - After a power loss during motion, or before the first full move, the position is unknown: *GoToLiftPercentage* to an intermediate position is refused and OPEN/CLOSE run the full travel time plus overrun. Reaching an endpoint makes the position known again.
  - The motor stops if its cutoff timer cannot be armed, and the command fails with `ACTION_DENIED`.
- **Cover switch cluster** for handling user input from window covering switches.
  Supports toggle/momentary switches, stop-on-repeat, stop button, local control, and remote device binding.
  The long-press duration is saved and survives reboots and updates.
- Relays now respond to *MoveToLevelWithOnOff*
  - Level = 0 -> Turn off relay
  - Level > 0 -> Turn on  relay
- Configurable multi-press factory reset count (set 1-255 or 0 to disable)
- **Indicator LED press confirmation**:  LEDs briefly flash on button press/release as visual feedback.
- **Push-button without relay** support for battery-powered scene switches
  - Switches auto-detect absence of relays and default to detached mode
  - New device: Moes 4-gang scene switch (`REMOTE_MOES_SWITCH_TS0044`)
- **Battery measurement & reporting** (Zigbee `genPowerCfg` cluster) for Telink devices
- **Deep retention sleep** for Telink end devices

### Changes

- **Settings reset when updating from upstream or pre-release fork firmware.**
  The first boot after the OTA update clears switch and relay settings (switch mode, detached mode, long press, startup mode, indicator modes, inching), cover calibration and position, a custom config string, the network LED setting and the multi-press reset count.
  The device then runs with its board's default config, like a fresh flash. Network membership, binds and groups are kept.
- Add `D<N>` config option to customize button debounce delay in milliseconds
- **Generated integrations** (Zigbee2MQTT, ZHA, HOMEd) configure the device through the Device Config (`0xFC10`) and Device Diagnostics (`0xFC11`) clusters, and send the fork manufacturer code `0x1A2B` on every fork-private cluster (`0xFC01`, `0xFC02`, `0xFC10`, `0xFC11`). The network LED switch became the status LED mode (`network`, `off`, `on`); diagnostic counters carry the cluster's attribute names.
- Covers in Zigbee2MQTT show 100 = open (the device reports ZCL lift %, 0 = open); an unknown position shows as no value in ZHA, while Zigbee2MQTT keeps the last position. Open and close times, deadzones and overrun are exposed in all three integrations, and the cover-switch long press in HOMEd.
- The GPIO probe board has a Zigbee2MQTT converter (model `TS011F-probe`) for `helper_scripts/probe_discovery.py`.
- A GPIO probe image may carry the relay and switch features: a config string written over Zigbee drives the pins it claims while the probe covers the rest. `OUTLET_BSEED_TS011F_PROBE` ships with button `B4` (momentary), relay `B5` and relay indicator `C4` (`SB4u;RB5;IC4;M;`), and its Zigbee2MQTT definition exposes the relay, the indicator modes and the button next to the probe.
- **Relay inching** (`InchingDuration`) pulses the relay for every switch-on: Zigbee On, Toggle, *MoveToLevelWithOnOff*, bindings, local switches and startup ON.
- **Relay interlock** (`K<mask>[:<ms>]` config option) is defined by the config string only; Zigbee can add an ungrouped relay to a `K` group but cannot change config-defined groups. A relay that joins a group while another member is on switches off. `K` without `:<ms>` waits 200 ms between one relay switching off and the next switching on.
- Relay settings (startup mode, indicator modes, inching, interlock) reset when the config string changes the relay behind an endpoint or the relays of an interlock group; relay pins, dead times and cover relay pairs do not reset them.
- *OnWithTimedOff* honours "accept only when on", never shortens a running timed-off and treats an on time of 0xFFFF as "stay on". On a relay with inching the inching duration caps the on time.
- **Bi-stable (latching) relays** have been reworked
  - They now use proper pulses instead of continuously driving the coil
  - Pressing multiple buttons will toggle the relays with small delays in-between (safe)
  - Add `SLP;` to the config string for simultaneous toggles (risky, might damage the device)
- **Power management for battery end devices**
  - Reduced TX power (~3 dBm instead of ~10 dBm) to save battery
  - Poll rate controller: fast polling + slow polling support
  - PowerCfg cluster for battery level monitoring
  - PollCtrl cluster to allow settings poll rate via ZCL
  - Add `BT<pin>` config option to enable battery mode

### Bugs

- **Fixed**
  - Latching relays not working with off_pin A0
  - Silabs version updates not working
  - Telink End_device unreachable from Z2M after a while ([#217](https://github.com/romasku/tuya-zigbee-switch/issues/217))
  - AC noise affecting Telink GPIO
  - Changing device type breaks Silabs NVM data
  - Reset needed 11 presses instead of 10
  - ZHA quirk silently failed to apply on relay-less devices with LED indicators (scene remotes), leaving firmware attributes unreachable from the UI
  - Silabs device dead after flashing when its SPI flash chip's JEDEC id was not in the bootloader's table (e.g. a ZBMINIL2 hardware variant with a different flash). The spiflash bootloader now falls back to a generic profile for any standard JEDEC SPI-NOR flash instead of failing storage init, so unknown-but-standard chips no longer need a per-chip bootloader build ([#422](https://github.com/romasku/tuya-zigbee-switch/issues/422))
  - Silabs: bindings to Zigbee groups ignored when emitting commands
- **New**
  - SONOFF ZBMINIL2 version updates broken?

## v1.1.2

_Bug-fix update_

### Bugs

- **Fixed**
  - Setting 'long press duration' to 0ms crashes device
  - Can't change device imageType in config string
  - Option 'Relay indicator - manual on' is not kept after reboot
  - Relay indicators sometimes go out-of-sync ([#38](https://github.com/romasku/tuya-zigbee-switch/issues/38))

## v1.1.1

_Critical bug-fix update for previous version_

### Changes

- Added a **hardware watchdog** on Telink devices that automatically reboots the device when it's stuck

### Bugs

- **Fixed:** Floating pin (or other device conditions) freezes the device

## v1.1.0

_Contains **substantial restructuring** of the firmware architecture, but doesn't bring new features_

### Changes

- Updated **Telink SDK** to v3.7.2.0 for **stability**
- Added support for **Silabs chips** by introducing **HAL middleware**
- Replaced GPIO polling with GPIO interrupts for **power efficiency**
- Improve **versioning** to include commit hash
- Many more technical improvements

### Bugs

- **Fixed**
  - Old SDK freezes device in some network conditions
- **New**
  - Floating pin (or other device conditions) **freezes device**
  - Setting 'long press duration' to 0ms **crashes device**
  - Can't change device imageType in config string
  - Option 'Relay indicator - manual on' is not kept after reboot
  - Relay indicators sometimes go out-of-sync ([#38](https://github.com/romasku/tuya-zigbee-switch/issues/38))
  - Telink End_device unreachable from Z2M after a while ([#217](https://github.com/romasku/tuya-zigbee-switch/issues/217))
  - Switch randomly toggles on TLSR8253 512KB devices ([#289](https://github.com/romasku/tuya-zigbee-switch/issues/289))
    (HOBEIAN and Zbeacon)
  - _Power-on behavior_ doesn't fully work on some devices
  - _momentary_nc_ not working after power loss.  
    (Apply the setting again)

## v1.0.21

### Changes

- Keep device configuration (user settings) when it is removed from the network

### New features

- Add support for Zigbee commands:
  - **off_with_effect** (0x40)
  - **on_with_recall_global_scene** (0x41)
- Add support for **normally-closed momentary buttons**
- Add **action states for toggle buttons**: position_on and position_off

## v1.0.20

### Changes

- (technical) Updated memory map: moved NV items from ZCL to APP.  
  **Due to this change, device configuration (user settings) may reset after OTA update.**

### Bugs

- **Fixed**
  - Canging config string crashed 3-4 gang devices
  - Detached mode didn't work for Toggle switches

## v1.0.19

### New features

- Add support for the **levelCtrl** cluster
  - This enables brightness control of compatible Zigbee bulbs via Zigbee binding.
  - The feature works only for momentary switches using long press: once a long press is detected, brightness will begin to slowly change. Each subsequent long press reverses the direction (increase/decrease).
  - Requires manual update of converters and reconfiguration.

### Changes

- Increase the number of **presses required to reset the device to 10.**
- Update manufacturer names to match the stock firmware.  
  (requires interview; but it's not mandatory, as backwards compatibility is kept)

### Bugs

- New bug: detached mode doesn't work for Toggle switches

## v1.0.18

- Partly fix an issue where setting the config string could brick the device.
- Technical: introduce a method to update data stored in NVRAM in new releases.

## v1.0.17

- Fix once again power on behavior = OFF not working if toggle in pressed state during boot.

## v1.0.16

- Add new toggle modes: TOGGLE_SMART_SYNC/TOGGLE_SMART_OPPOSITE (requires re-download of `switch_custom.js`).

## v1.0.15

- Add support for Zigbee groups. Read [doc](/docs/usage/endpoints.md) for details about endpoints.

## v1.0.14

- Improve code logic for Indicator LED on for switches.

## v1.0.13

- Fix power on behavior = OFF not working if toggle in pressed state during boot.
- Add way to control network state led state (requires re-download of `switch_custom.js`).

## v1.0.12

- Fix led indicator state in manual mode not preserved after reboot.
- Add forced device announcement after boot to make sure device is seen as "available" as soon as it boots.
- Restored device pictures in z2m (requires re-download of `switch_custom.js`).
- Cleaned-up z2m converter (fix typos, inconsistent names, etc.). **Warning!** This may break your automations as it changes .
  property names (requires re-download of `switch_custom.js`).

## v1.0.11

- Improve join behaviour by decreasing timeout between tries to join.
- Fix leave network: now device will send LeaveNetwork command properly.
- Display firmware version in a human-readable form.

## v1.0.10

- Add support for bi-stable relays controlled by 2 pins.
- Fix Led indicator mode not preserved after reboot.

## v1.0.9

- Fix reporting of indicator led status.

## v1.0.8

- Add support for indicator leds.
- Add way to force momentary mode as default via config.

## v1.0.7

- Add SUSPEND-based sleep to EndDevice firmware to decrease power usage ~10x.

## v1.0.6

- Add way to change device pinout on the fly, to allow easier porting of firmware .

## v1.0.5

- Keep status LED on when device is connected.
- Add separate firmwares for End Device/Router.
- Improve device boot time significantly by removing unnecessary logs .

## v1.0.4

- Fix bug that caused report to be sent every second.

## v1.0.3

- Add support of startup behaviour: ON, OFF, TOGGLE, PREVIOUS.
- Add support of button actions: 'released', 'press', 'long_press'. This is only useful for momentary (doorbell-like) switches.

## v1.0.2

- Add way to reset the device by pressing any switch button 5 times in a row .
- Fix support for ON_OFF, OFF_ON actions.
