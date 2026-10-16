# Device database

Every supported device is one entry in [`device_db.yaml`](../../device_db.yaml),
validated by [`device_db.schema.json`](../../device_db.schema.json). Adding a
device usually means adding one entry ([porting.md](./porting.md)). The file
is large: find an entry with `grep -n '^<KEY>:' device_db.yaml` and read only
that block.

All fields except `features` are required (use `null` where the schema allows
it). After editing, run `make tools/check_device_features` and
`make unit_tests`.

[off_conv]: https://github.com/Koenkk/zigbee-herdsman-converters/tree/master/src/devices

## Identity and description

| Field | Description |
| --- | --- |
| **KEY** (e.g. `SWITCH_BSEED_TOUCH_TS0002`) | Board name: `BOARD=<KEY>` in the build, directory name under `bin/` |
| `human_name` | Full device name, shown in [supported_devices.md](../supported_devices.md) |
| `category` | `module`, `switch`, `remote`, `outlet`, `board`, `plug`, `din_relay`, `cover`; shown in supported_devices.md |
| `power` | `mains`, `DC`, `USB`, `AA`, `AAA`, `CR2016`, `CR2025`, `CR2032`, `CR2430`, `CR2450`, `LR23A`, `18650`; shown in supported_devices.md |
| `neutral` | `required`, `optional`, `without` or `null`. Descriptive; no build step or generator reads it |
| `output` | AC output: `relay`, `relay_latching`, `triac`, `nothing` or `null`. Descriptive; no build step or generator reads it |
| `tuya_module` | Tuya module inside the device (`ZTU`, `ZT3L`, `ZS3L`, `TYZS3`, ...). Descriptive |
| `status` | `fully_supported`, `mostly_supported`, `in_progress`, `unsupported`; shown in supported_devices.md |
| `info` | Short explanation of the status and findings |
| `threads` | Link(s) to the related issue or pull request |
| `store` | Link(s) to buy the exact device |

## Platform and build

| Field | Description |
| --- | --- |
| `mcu_family` | `Telink`, `Silabs` or `NXP`. Selects the platform makefile (`src/telink/`, `src/silabs/`); a probe board must be `Telink` |
| `mcu` | `TLSR825x`, `TLSR8253`, `TLSR8253F512AT32`, `TLSR8258`, `EFR32MG21A020F768IM32`, `EFR32MG22C224F512GN32`, `JN5169` or `null`. Passed to the Silabs project generation (`make silabs/gen MCU=...`, `slc --with`) |
| `device_type` | `router` (L+N devices) or `end_device` (L-only and battery devices). `make_scripts/make_all.sh` builds a router image for every board and an end-device image too for `end_device` boards |
| `build` | `yes` / `no`. `make_scripts/make_all.sh` and the converter, quirk and extension generators skip `no` boards (the Zigbee2MQTT converter keeps probe boards); `make unit_tests` parses the configs of `yes` boards only |
| `features` | Optional. Features compiled into the image: `relay` (`R` tokens), `switch` (`S`), `cover` (`C`, `X`), `battery` (`BT`), `meter` (`E`; not with `probe`), `probe` (GPIO probe image, [porting.md](./porting.md#gpio-probe-devices-that-cannot-be-opened); only with `relay`/`switch`, Telink router), `interlock` (`K` tokens and each cover's motor relays; added automatically to boards with a cover, the probe, a `K` token or more than one relay). Omitted: derived from the tokens of `config_str` and `alt_config_str`. Without `interlock` the relay cluster's `InterlockGroup` stays 0 and other values are rejected |
| `config_str` | Default device config string `manufacturer;model;tokens...` ([porting.md](./porting.md#config-string)). `manufacturer` and `model` become the Zigbee manufacturer and model on the custom firmware: OTA updates match the manufacturer, converters match the model |
| `alt_config_str` | Alternative config string(s) for hardware variants. Their tokens count when features are derived, and `make unit_tests` reports those the parser rejects |
| `firmware_image_type` | OTA image type of the custom firmware, unique per board. `make tools/unused_image_type` prints the next free value |

`make tools/check_device_features` fails when a config uses a token whose
feature the board does not compile. A device whose stored config uses such a
token boots the recovery graph with Device Config `LastError`
`FEATURE_MISSING` ([architecture.md](./architecture.md#recovery-graph)).

## Stock firmware and converters

| Field | Description |
| --- | --- |
| `stock_model_name` | Zigbee model on the stock firmware (e.g. `TS0002`), string or list; shown in supported_devices.md |
| `stock_manufacturer_name` | Zigbee manufacturer on the stock firmware (`_TZ3000_xxxxxxxx`). The board's OTA index entries are filtered to it, the old names and the config string's manufacturer |
| `stock_manufacturer_id` | Manufacturer code of the stock OTA image (usually 4417), from the Zigbee2MQTT debug log of an OTA check. Needed for the from-stock OTA file |
| `stock_image_type` | Image type of the stock OTA image (usually 54179), from the same log. Needed for the from-stock OTA file |
| `stock_converter_manufacturer` | Manufacturer name in the official Zigbee2MQTT converter ([herdsman converters][off_conv]), not the Zigbee manufacturer. With `stock_converter_model` it adds OTA support to the stock converter (`tuya_with_ota.js`) |
| `stock_converter_model` | Model name in the official Zigbee2MQTT converter, not the Zigbee model. Also the model the custom converter reports unless `override_z2m_device` is set |
| `override_z2m_device` | Model reported by the custom converter instead of `stock_converter_model` (a `whiteLabel` model from the official converter), for the device's own name and picture |
| `old_manufacturer_names` | Manufacturer names earlier custom firmware used; added to the OTA index `manufacturerName` filter and the ZHA quirk |
| `old_zb_models` | Model names earlier custom firmware used; matched by the generated converters, quirk and extension |
