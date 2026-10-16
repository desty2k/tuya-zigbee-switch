# Frequently asked questions

### Something does not work. What should I check first?

1. Make sure the [integration files](./updating.md#integration-files) match
   the firmware on the device: the fork's files from the
   `refactor/input-pipeline` branch, never upstream converters.
2. Restart Zigbee2MQTT (or Home Assistant) after replacing them.
3. Give the device a strong signal and, for a no-neutral switch, a sufficient
   load ([updating.md](./updating.md#router-or-end-device)).
4. Interview (`i`) and reconfigure the device.
5. Check `device_config_status` and `device_config_last_error`
   ([below](#the-device-shows-up-as-recovery)) and the
   [diagnostics counters](#how-do-i-read-the-diagnostics-counters).
6. Read [known_issues.md](./known_issues.md).

Report what remains on the fork's
[issue tracker](https://github.com/desty2k/tuya-zigbee-switch/issues), with the
board name, firmware version, config string and the diagnostics counters.

### How do the relay, action and binded modes work?

See [configuration.md](./usage/configuration.md#switch-endpoints-s-buttons).

### How do I use double clicks or holds in automations?

See [button_events.md](./usage/button_events.md).

### The device shows up as "recovery"

The device stores a [config string](./usage/config_string.md) its firmware
cannot run, so it booted the recovery graph: Basic reports manufacturer
`unknown` and model `recovery`, only endpoint 1 exists, and every relay is off.
`device_config_last_error` names the reason
([table](./usage/config_string.md#when-a-config-string-is-rejected)); the most
common one is `feature_missing` after installing an image built for another
board.

The device is still on the network and still accepts OTA updates and config
writes. Two ways back:

- **Write a corrected config string.** Integrations do not recognise the
  `recovery` model, so use Zigbee2MQTT's Dev console: endpoint 1, cluster
  `0xFC10` (64528), attribute `0x0002`, type long character string (`0x44`),
  manufacturer code `0x1A2B` (6699). The device restarts with the new string.
- **Install the right image over OTA.** Zigbee2MQTT offers updates only to
  devices with a definition, so add a temporary external converter:

  ```js
  module.exports = {
      zigbeeModel: ["recovery"],
      model: "recovery",
      vendor: "Tuya-custom",
      description: "Custom firmware in its recovery graph",
      extend: [],
      ota: true,
  };
  ```

  The published index entries match devices by manufacturer name, and
  `unknown` matches none of them. Copy the board's entry from the fork's index
  into a local index file without its `manufacturerName` field, point
  `zigbee_ota_override_index_location` at that file, and update. Remove the
  temporary converter and restore the index afterwards.

### How do I read the diagnostics counters?

Device Diagnostics (`0xFC11`) on endpoint 1 counts input, button event and
network activity since boot ([reference](./usage/endpoints.md#device-diagnostics-0xfc11)).
Zigbee2MQTT exposes each counter (`uptime_ms`, `gpio_edges`, `gpio_dropped`,
`button_events`, `zb_dropped`, `network_losses`, …) and reads it on request:
press refresh in the Exposes tab, or publish
`{"gpio_dropped": ""}` to `zigbee2mqtt/<device>/get`. ZHA has them as
diagnostic sensors, disabled by default; HOMEd lists them on endpoint 1.

What to look for:

| Counter | Points to |
| --- | --- |
| `gpio_dropped`, `gpio_rearm_hits` rising | noisy wiring or a bouncing contact; raise the debounce |
| `button_events` rising without presses | interference on the switch wires; raise the debounce |
| `zb_expired`, `zb_dropped`, `zb_send_failed` | button events lost on the way to the coordinator: weak signal or the device not joined |
| `network_losses`, `network_joins` rising | unstable connection or supply (no-neutral load too small) |
| `uptime_ms` low or restarting | the device rebooted: power cuts or a crash |

### How do I reset the device?

- Press any switch or cover-switch button 10 times in a row, with pauses
  shorter than the multi-click gap (350 ms by default). The count is
  `multi_press_reset_count` (0–10, 0 turns it off). A rocker counts one press
  per on-and-off cycle.
- Or hold the onboard reset button, if the board has one, for 2 s.

The device leaves the network, starts pairing (its LED blinks) and releases
the child lock. Details: [configuration.md](./usage/configuration.md#factory-reset).

### What is a force OTA index?

An index whose images install over any fork version, including the same one,
so Zigbee2MQTT always shows an update. Use it to switch between router and
end-device firmware or to reinstall, then return to the normal index. The
image is the same as in the normal index. See
[updating.md](./updating.md#ota-index).

### Will my other devices still receive updates?

Yes. The custom index is added to the official one: supported devices get
images from this repository, all others their official updates.

### Can I go back to the stock firmware?

Only by wire, and only with a memory dump of the stock device. Some dumps are
in [`bin/_factory/`](../bin/_factory/). Follow [flashing/](./flashing/) with
the dump instead of the custom image.

### The device does not respond at all

If it does not join, does not react to buttons and shows no LED activity,
flash it by wire: [flashing/](./flashing/). A device that joins as
`recovery` does not need this ([above](#the-device-shows-up-as-recovery)).
