# Installing and updating over the air

This page covers converting a supported device from its stock firmware,
updating between fork releases, and switching between router and end-device
firmware, all over Zigbee. Wired flashing is in [flashing/](./flashing/).

> [!IMPORTANT]
> - This firmware is the fork [desty2k/tuya-zigbee-switch](https://github.com/desty2k/tuya-zigbee-switch),
>   a separate project from upstream romasku/tuya-zigbee-switch.
> - Moving a device from upstream firmware (or 1.x) to fork 2.x resets its
>   settings. Network membership, binds and groups survive.
> - Settings survive OTA updates between fork 2.x releases.
> - The fork's converters, quirk and extension match fork firmware only.
>   Update them together with the firmware, and never mix them with upstream
>   converters.

> [!CAUTION]
> Every board is published as a release candidate (2.0.0-rc.5), except the
> Sonoff ZBMINIL2 ([known_issues.md](./known_issues.md#release-status)). Only
> the boards listed as tested in the [readme](../readme.md#devices) ran 2.x on
> hardware.
> A faulty image can leave a device that needs wired flashing:
> [flashing/](./flashing/). Restoring stock firmware needs a dump of the stock
> device ([`bin/_factory/`](../bin/_factory/)).

Integration support:

- **Zigbee2MQTT** (recommended): converts, updates and exposes every setting,
  including the [config string](./usage/config_string.md).
- **ZHA**: updates and exposes the settings except the config string; it cannot
  re-interview a device (remove and pair it again instead).
- **HOMEd**: exposes the settings except the config string and receives no
  [button events](./usage/button_events.md); firmware updates only from local
  files.

## Integration files

The firmware's Zigbee interface is described by generated files on the
`refactor/input-pipeline` branch. Install the set that matches the firmware you
install, and replace it whenever you update the firmware.

| Integration | File | Install to |
| --- | --- | --- |
| Zigbee2MQTT 2.x | [`zigbee2mqtt/converters/`][converters]: `switch_custom.js` and `tuya_with_ota.js` | `external_converters/` in the Zigbee2MQTT data folder (next to `coordinator_backup.json`) |
| Zigbee2MQTT 1.x | [`zigbee2mqtt/converters_v1/`][converters_v1]: the same two files | as above |
| ZHA | [`zha/switch_quirk.py`][quirks] | `custom_zha_quirks/` in the Home Assistant config folder |
| HOMEd | [`homed/tuya-custom.json`][extensions] | `/opt/homed-zigbee/external` (or the `device/external` path of `homed-zigbee.conf`) |

`switch_custom.js` describes devices running this firmware;
`tuya_with_ota.js` adds the OTA update to the stock Tuya definitions of
supported models, so they can be converted. Restart the integration after
replacing a file.

ZHA needs the quirk path and the OTA index in `configuration.yaml`:

```yaml
zha:
  enable_quirks: true
  custom_quirks_path: ./custom_zha_quirks/
  zigpy_config:
    ota:
      extra_providers:
        - type: z2m
          url: <index URL>
```

## OTA index

The index lists the firmware images. Point Zigbee2MQTT at it under
**Settings → OTA updates → OTA index override file name**, or in
`configuration.yaml`, then restart Zigbee2MQTT:

```yaml
ota:
  zigbee_ota_override_index_location: <index URL>
```

The custom index is added to the official one, so other devices keep receiving
their own updates.

| Index | URL | Contents |
| --- | --- | --- |
| Router | `https://raw.githubusercontent.com/desty2k/tuya-zigbee-switch/refs/heads/refactor/input-pipeline/zigbee2mqtt/ota/index_router.json` | router images for every board, plus conversion images for stock Tuya devices of supported models |
| Router, force | `https://raw.githubusercontent.com/desty2k/tuya-zigbee-switch/refs/heads/refactor/input-pipeline/zigbee2mqtt/ota/index_router-FORCE.json` | router images that install over any fork version, including the same one |
| End device | `https://raw.githubusercontent.com/desty2k/tuya-zigbee-switch/refs/heads/refactor/input-pipeline/zigbee2mqtt/ota/index_end_device.json` | end-device images for no-neutral boards and battery devices, plus conversion images |
| End device, force | `https://raw.githubusercontent.com/desty2k/tuya-zigbee-switch/refs/heads/refactor/input-pipeline/zigbee2mqtt/ota/index_end_device-FORCE.json` | end-device images that install over any fork version |

Use a normal index day to day. The force indexes always offer an update; use
one only to switch between router and end-device firmware or to reinstall, then
go back to the normal index.

The images themselves are in [`bin/`][bin] on the same branch:
`bin/router/<BOARD>/` and `bin/end_device/<BOARD>_END_DEVICE/`.

## Converting a stock device (Telink only)

Silabs devices are converted by wire: [flashing/silabs.md](./flashing/silabs.md).

1. Find the device in [supported_devices.md](./supported_devices.md) and read
   [known_issues.md](./known_issues.md).
2. Install the [integration files](#integration-files) and a normal
   [OTA index](#ota-index), then restart Zigbee2MQTT or Home Assistant.
3. Give the device a strong signal: move it close to the coordinator or a
   router. A no-neutral switch needs a sufficient load (see
   [router or end device](#router-or-end-device)).
4. In Zigbee2MQTT's OTA page, check the device for updates, then start the
   update. The progress bar may stay at 100 % for a while.
5. After the update the device leaves the stock identity and joins again:
   permit joining. Its LED blinks while it is not joined.
6. Interview the device (`i`) and reconfigure it. ZHA: remove and pair it
   again.

## Updating between fork releases

1. Read [changelog_fw.md](./changelog_fw.md) for the new version.
2. Check the device for updates in the OTA page and start the update.
3. When it finishes, replace the [integration files](#integration-files) with
   the ones from the same branch and restart the integration.
4. Interview and reconfigure the device. Settings, binds and groups stay.

A device several versions behind updates directly to the newest version.

## Router or end device

Every board has a router image. Boards without a neutral wire, and battery
devices, also have an end-device image.

- **Router**: relays Zigbee traffic for other devices and answers fastest.
  Needs a constant supply.
- **End device**: does not route and draws less power, which helps no-neutral
  switches and devices that are often switched off. It does not receive group
  commands ([known_issues.md](./known_issues.md)).

> [!WARNING]
> A no-neutral (L-only) switch draws its supply through the load, on any
> firmware. This firmware draws more than the stock one: count on more than
> about 4 W of load for an end device and 8 W for a router (estimates). A smart
> bulb draws under 1 W when off and is not enough without a bypass capacitor.

To switch a device between router and end-device firmware:

1. Point Zigbee2MQTT at the force index of the type you want (router force or
   end device force).
2. Check the device for updates and install the update.
3. On its first start as the other type the device clears its network data and
   leaves the network, because the stack's network data of one type does not
   fit the other: permit joining and pair it again. Its binds and group
   memberships are gone; set them up again.
4. Interview the device and confirm its type changed.
5. Point Zigbee2MQTT back at the normal index of the new type.

## Faster OTA updates

Updates run slowly by default to spare the network. With a good signal and a
quiet network, lower **Settings → OTA updates → default maximum data size**
and **image block response delay** in Zigbee2MQTT (50 bytes and 50 ms finish
in under 5 minutes). See the
[Zigbee2MQTT OTA documentation](https://www.zigbee2mqtt.io/guide/usage/ota_updates.html#advanced-configuration).

[converters]: https://github.com/desty2k/tuya-zigbee-switch/tree/refactor/input-pipeline/zigbee2mqtt/converters
[converters_v1]: https://github.com/desty2k/tuya-zigbee-switch/tree/refactor/input-pipeline/zigbee2mqtt/converters_v1
[quirks]: https://github.com/desty2k/tuya-zigbee-switch/tree/refactor/input-pipeline/zha
[extensions]: https://github.com/desty2k/tuya-zigbee-switch/tree/refactor/input-pipeline/homed
[bin]: https://github.com/desty2k/tuya-zigbee-switch/tree/refactor/input-pipeline/bin
