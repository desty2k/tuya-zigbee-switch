# Building

The firmware is built per board: `board.mk` reads the board's entry in
[`device_db.yaml`](../../device_db.yaml) and builds one image with

- the board's **features** (`helper_scripts/board_features.py features <BOARD>`,
  see [device_db_explained.md](./device_db_explained.md)): only the code of
  those features is compiled in (`src/features.mk`), so two images for the
  same MCU can differ in code, not only in the default config string;
- the board's `config_str` as the default **device config string**, used until
  another one is written over Zigbee;
- the board's `firmware_image_type`, `device_type` (router or end device) and
  MCU.

How the pieces fit together is described in [architecture.md](./architecture.md).

## Setup

Linux is recommended; the scripts use `apt` (Debian/Ubuntu).

1. Clone the repository and create a branch for the change.
2. Run `make_scripts/make_install.sh` once: it installs the system packages
   (`make`, `python3`, `yq`, `gcc-arm-none-eabi`, ...) and runs `make setup`,
   which downloads the Telink and Silabs toolchains and SDKs
   (`telink_tools/`, `silabs_tools/`) and creates the Python virtual
   environment.
3. `source .venv/bin/activate` before running the generators and tests.

For the host build and the tests only, the toolchains are not needed:
`make setup_venv` and a host C compiler suffice.

### Dev container

`.devcontainer/` provides an image with the dependencies, the VS Code
extensions and the virtual environment. Open the folder in VS Code and choose
**Reopen in Container**.

## Workflow

1. Change code; build and test on the host:
   `make stub/build`, `make unit_tests`, `make tests` ([tests.md](./tests.md)).
2. `make format` (uncrustify over `src/`; CI rejects unformatted code).
3. After changing `device_db.yaml`: `make tools/check_device_features`.
4. Build the board: `BOARD=<key> make board/build`. For a debug image with
   UART output: `BOARD=<key> DEBUG=1 make board/build-firmware`
   ([debugging.md](./debugging.md)).
5. Install it on a device ([updating.md](../updating.md) or
   [flashing/](../flashing/)) and test it.
6. Regenerate the integrations if the Zigbee interface or the device database
   changed (`make tools/update_converters`, `make tools/update_zha_quirk`,
   `make tools/update_homed_extension`, `make tools/update_supported_devices`)
   and commit the generated files separately.
7. Add the change to the `## Upcoming` section of
   [changelog_fw.md](../changelog_fw.md).

Images under `bin/` and the OTA index entries in `zigbee2mqtt/ota/` are
published as part of a release: see [release.md](./release.md).

## Make targets

`make help`, `make tools/help`, `make board/help`, `make stub/help`,
`make telink/help` and `make silabs/help` print the targets of each makefile.

### Host build and tests

| Command | Description |
| --- | --- |
| `make stub/build` | Host simulation binary `build/stub/stub_device` |
| `make stub/build_end_device` | End-device stub `build/stub/stub_end_device` |
| `make stub/build_switch_only` | Stub with only `relay switch` |
| `make stub/build_probe` | Stub with `probe relay switch interlock` |
| `make stub/run` | Build and start the stub REPL |
| `make stub/clean` | Remove the stub build and NVM data |
| `make tests` | Build the four stub variants and run pytest |
| `make unit_tests` | Build and run the native unit tests in `tests/unit/` |
| `make format` | uncrustify every `.c` / `.h` in `src/` |

### Boards (`board.mk`)

`BOARD` is a key of `device_db.yaml`; `DEVICE_TYPE` defaults to the board's
`device_type` and can be overridden (`DEVICE_TYPE=end_device`).

| Command | Description |
| --- | --- |
| `BOARD=<key> make board/build` | Refuse probe boards, delete old images of the board, build, generate the OTA files, add them to the Zigbee2MQTT indexes |
| `BOARD=<key> make board/build-firmware` | Build the image only (`bin/<device_type>/<board>/`, `.bin` for Telink, `.s37` for Silabs) |
| `BOARD=<key> make board/generate-ota-files` | Normal, forced and (Telink) from-stock `.zigbee` OTA files |
| `BOARD=<key> make board/update-indexes` | Add the board's OTA files to `zigbee2mqtt/ota/index_*.json`; refuses probe boards |
| `make_scripts/make_all.sh` | Clear the indexes, build every board with `build: yes` (router, plus end device for `end_device` boards), regenerate converters, quirk and extension |
| `make_scripts/make_debug_single.sh` | `DEBUG=1` router and end-device builds of one board (edit `DEVICE` in a copy) |

### Platforms

| Command | Description |
| --- | --- |
| `make setup` | Telink and Silabs tools, Python virtual environment |
| `make setup_venv` | Python virtual environment only |
| `make telink/tools/all` | Download the Telink toolchain and SDK |
| `make telink/build` | Build the Telink firmware with the makefile defaults |
| `make silabs/tools/all` | Download SLC CLI, Gecko SDK and Commander |
| `make silabs/trust` | Trust the SDK signature (first setup) |
| `make silabs/gen`, `make silabs/build` | Generate the SLC project, build |
| `make silabs/install` | Flash with Commander |
| `make silabs/bootloader_build`, `make silabs/bootloader_install` | Silabs bootloader |

### Generators and checks (`tools.mk`)

| Command | Description |
| --- | --- |
| `make tools/update_converters` | Zigbee2MQTT converters (`zigbee2mqtt/converters/`, `converters_v1/`) |
| `make tools/update_zha_quirk` | `zha/switch_quirk.py` |
| `make tools/update_homed_extension` | `homed/tuya-custom.json` |
| `make tools/update_supported_devices` | [supported_devices.md](../supported_devices.md) |
| `make tools/check_device_features` | Every board's config tokens against its features, probe board rules, `ALL_FEATURES` against the schema enum |
| `make tools/check_management_protocol` | Files generated from `helper_scripts/management_protocol.json` are up to date |
| `make tools/unused_image_type` | Next free `firmware_image_type` |
| `make tools/clean_z2m_index` | Empty the Zigbee2MQTT OTA indexes |
| `make tools/freeze_ota_links` | Replace the branch name in index URLs with the current commit (release step) |

## Further reading

- [architecture.md](./architecture.md)
- [porting.md](./porting.md)
- [device_db_explained.md](./device_db_explained.md)
- [debugging.md](./debugging.md)
- [tests.md](./tests.md)
- [release.md](./release.md)
