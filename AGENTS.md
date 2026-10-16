# AGENTS.md

Operating guide for AI agents working in this repository.

## What this project is

Custom Zigbee firmware for cheap Tuya-style wall switches, relay modules, sockets
and remotes, replacing the vendor firmware. It fixes vendor bugs (notably the
"two buttons pressed at once toggles one relay" defect) and adds detached mode,
outgoing binds, configurable long press, power-on behaviour, multiple reset
options and OTA updating.

- Two MCU families: **Telink** (TLSR8258 / TLSR8253) and **Silabs**
  (EFR32MG21 / EFR32MG22).
- 160+ supported devices, all described in one database: `device_db.yaml`.
- One firmware binary per board. The board's *features* (relay, switch, cover,
  battery, meter, probe, interlock; `features` in `device_db.yaml`, otherwise derived from its
  config tokens) are selected at build time through `src/features.mk`; code of
  other features is left out of the image.
- The hardware layout is **not** compiled in: it comes from a *device config
  string* (pins, buttons, relays, LEDs) stored in NVM and parsed at boot by
  `src/device_config/config_parser.c`. A token for a feature the image lacks
  boots the recovery graph with Device Config LastError `FEATURE_MISSING`.
- Behaviour is configured at runtime through ZCL attributes, including custom
  manufacturer-specific ones.
- A host-native **stub** build simulates the whole firmware for tests.

## Context hygiene (read this first)

The repository contains generated files large enough to destroy a context window.
Never read them whole.

| Path | Size | Rule |
| --- | --- | --- |
| `device_db.yaml` | ~6 200 lines | **Never read whole.** Grep the device key, then read that block only |
| `homed/tuya-custom.json` | ~46 500 lines | Generated. Never read |
| `zigbee2mqtt/converters/switch_custom.js`, `converters_v1/` | ~15 700 lines | Generated. Never read; inspect the template instead |
| `zigbee2mqtt/ota/index_*.json` | 1 800–5 100 lines | Generated. Never read |
| `zha/switch_quirk.py` | ~1 000 lines, generated | Inspect `helper_scripts/templates/zha_quirk.py.jinja` instead |
| `docs/supported_devices.md` | 73 KB, generated | Never read; regenerate if needed |
| `bin/**` | firmware artifacts | Never read |
| `telink_tools/`, `silabs_tools/`, `build/` | vendor SDKs and build output | Never read |

Working with `device_db.yaml`:

```bash
grep -n '^SWITCH_BSEED_TOUCH_TS0002:' device_db.yaml     # find the block
sed -n '1234,1274p' device_db.yaml                        # read only that block
grep -n 'config:' device_db.yaml | head -5                # see the field shape once
grep -c '^[A-Z_]*:' device_db.yaml                        # count devices, don't list them
```

General rules for every file:

- Check size before reading: `wc -l <file>`. Over ~800 lines, read ranges.
- Use the read tool's `offset` / `limit` instead of reading a whole large file.
- Search with `grep -n` and cap output: `grep -n -m 20`, `| head -40`,
  `--max-count`, `-c` for counts, `-l` for filenames only.
- Prefer symbol search and AST search tools over dumping files.
- Never `cat` a file you have not sized. Never pipe a generated file anywhere.
- Regenerate artifacts instead of hand-reading them: `make tools/help` lists the
  generators.

## Project structure

| Path | Content |
| --- | --- |
| `src/app.c`, `src/app.h` | `app_init()` runs NVM migrations, `device_config_service_init()` and `parse_config()`; `app_task()` is the main-loop pump |
| `src/hal/` | Hardware abstraction layer headers — the only cross-platform contract |
| `src/base_components/` | Platform-independent logic: input pipeline (`gpio_edge_queue`, `button_input`, `button_dispatcher`, `gesture_fsm`, `action_mapper`), controllers (`relay_controller`, `relay_driver`, `interlock`, `cover_controller`), feedback (`indicator_feedback`, `led`, `child_lock`), services (`timer_service`, `diagnostics_service`, `meter_service`, `gpio_probe_service`) |
| `src/zigbee/` | Platform-independent ZCL cluster implementations |
| `src/device_config/` | Config service and parser, `device_composition`, `feature_wiring` (builds the graph and the recovery graph), `capability_state`, NVM item ids, migrations, reset |
| `src/telink/` | Telink platform: HAL impl, SDK glue, makefiles, OTA packaging |
| `src/silabs/` | Silabs platform: HAL impl, SLC project files, bootloader extension |
| `src/stub/` | Host build: HAL impl, REPL, machine-readable event output |
| `tests/` | pytest suite driving the stub binary over its REPL |
| `device_db.yaml` | Single source of truth for devices (+ `device_db.schema.json`) |
| `helper_scripts/` | Generators for converters, quirks, docs (+ `templates/`) |
| `board.mk`, `tools.mk`, `Makefile`, `make_scripts/` | Build orchestration |
| `docs/` | User and contributor documentation |
| `docs/refactor/` | Design specs for in-flight refactors; read the local `STATE.md` first |
| `bin/`, `zigbee2mqtt/`, `zha/`, `homed/` | Build and generator output — never hand-edited |

Layering rule: `src/base_components/` and `src/zigbee/` must not include platform
headers. They talk to hardware only through `src/hal/`. Platform-specific code
lives in `src/telink/`, `src/silabs/`, `src/stub/` and implements the HAL.
Data flow, ownership rules, execution contexts and extension checklists:
`docs/contribute/architecture.md`.

## Build and test

```bash
make stub/build                  # host simulation binary (fast, always do this first)
make tests                       # builds the stub variants, runs pytest
make stub/build_switch_only      # stub with only the relay and switch features
make tools/check_device_features # config tokens vs. board features, all boards
pytest tests/test_relay_cluster.py -v
make stub/run                    # interactive REPL
make format                      # uncrustify, required before commit
BOARD=SWITCH_BSEED_TOUCH_TS0002 make board/build
make telink/build                # needs telink_tools (make setup)
make silabs/build                # needs silabs_tools (make setup)
```

CI runs two jobs: an uncrustify `--check` over `src/`, and the test job
(`make tools/check_device_features`, `make unit_tests`, `make tests`). Both must
pass. Formatting is enforced, so run `make format` and commit the result.

Verification expectations for any code change:

1. `make stub/build` must succeed with no new warnings.
2. `make tests` must pass; add tests for new behaviour.
3. `make format` must produce no further diff.
4. Changes to Telink or Silabs paths that cannot be built locally must be stated
   as unverified in the summary.

## Testing model

- The stub implements the same HAL as real firmware and exposes a REPL used by
  `tests/client.py` and `tests/conftest.py`.
- Time is virtual and controllable (`freeze_time`, `step_time`), so tests are
  deterministic; never use wall-clock sleeps to wait for firmware timers.
- GPIO is simulated: `set_pin` drives inputs, `read_pin` observes outputs.
- Zigbee is simulated: attribute read/write, command injection, and machine events
  for outgoing commands, attribute changes and announcements.
- NVM is simulated in `stub_nvm_data/` and wiped between tests.
- Device hardware for a test comes from the `device_config` fixture — a config
  string, not a real device entry.
- Stub variants with other feature sets: `stub_switch_only`, `stub_probe`,
  `stub_end_device` (all built by `make tests`); select with `StubProc(cmd=...)`.
- Native unit tests in `tests/unit/` (`make unit_tests`) compile single modules
  with fakes from `tests/unit/support/` and parse every board's config string.
- Details: `docs/contribute/tests.md`.

## Code conventions

- C99. No dynamic allocation. Fixed-size arrays with explicit bounds.
- `snake_case`, module-prefixed public symbols (`relay_ctrl_submit`,
  `button_input_add`), `static` for everything not in the header.
- Formatting is uncrustify-defined (`uncrustify.cfg`); do not hand-format.
- Keep functions small and single-purpose; keep policy out of HAL and hardware
  access out of policy.
- Prefer explicit `uint8_t` / `uint16_t` / `uint32_t` and unsigned time
  arithmetic (`now - then`) so `hal_millis()` wraparound is handled naturally.
- Interrupt-context code does queue pushes and sets pending flags only; tasks are
  scheduled from the main loop.
- New ZCL attributes and clusters: add cluster ids to `src/zigbee/zcl_ids.h`,
  keep attribute and command definitions with their owning cluster, give a new
  standard cluster a Telink register function, and mirror them in the generator
  templates (checklist in `docs/contribute/architecture.md`).

## Comments and documentation policy

Code and docs describe the **current state**, never the journey.

Do not write:

- "previously we did X", "changed from Y", "kept for backward compatibility",
  "legacy", "new implementation", "old version below"
- commented-out code, or code behind a flag that is never enabled
- restatements of what the code literally does
- TODO lists inside source files

Do write:

- why a non-obvious constraint exists (hardware quirk, SDK behaviour, ZCL
  requirement, timing bound)
- units and ranges for timing and configuration values
- invariants a function relies on or maintains

When replacing an implementation, **delete the old one in the same change**. No
adapters, no parallel paths, no dead symbols.

This repository is the BSEED-focused fork (`desty2k/tuya-zigbee-switch`), a
separate project from upstream `romasku/tuya-zigbee-switch`; it is not merged
back. Compatibility guarantees:

- None towards upstream firmware: upstream's Basic `0xFF0x` management
  attributes, converters and NVM layouts are not preserved. Updating a device
  from upstream or pre-release fork firmware resets its settings (network
  membership, binds and groups survive).
- The fork's own generated converters (`zigbee2mqtt/`, `zha/`, `homed/`) must
  always match the firmware's Zigbee interface. Device management uses the
  fork-private clusters `0xFC10`/`0xFC11`; every fork-private cluster uses the
  single manufacturer code defined in `helper_scripts/management_protocol.json`.
- From the first fork release onward, settings survive OTA between fork
  releases: NVM layout changes need converting migrations, not clearing.
- OTA images must install safely; image types stay unique per board.

Documentation rules:

- `docs/` describes how the firmware behaves now.
- Version history goes only in `docs/changelog_fw.md`.
- Do not create new markdown files unless asked; update the relevant existing doc.
- Generated docs (`docs/supported_devices.md`) are regenerated, never edited.

## Versioning and NVM

- `VERSION` is the firmware version used for OTA and the Basic cluster.
- `NVM_MIGRATIONS_VERSION` must be bumped whenever a persisted struct layout or
  NVM item meaning changes, with a matching step in
  `src/device_config/nvm_migrations.c`. `hal_nvm_read` is size-exact: a struct
  that grows will simply fail to load, so migrations must delete or convert the
  affected items.
- NVM item ids live in `src/device_config/nvm_items.h` and are never reused for a
  different purpose.

## Adding a device

Add one block to `device_db.yaml` (see `docs/contribute/device_db_explained.md`
and validate against `device_db.schema.json`), run `make tools/check_device_features`,
then regenerate outputs with the `make tools/...` targets. Do not edit `zigbee2mqtt/`, `zha/`, `homed/` or
`docs/supported_devices.md` by hand.

## Git

- Branch per change; do not commit to `main`.
- Commit only files relevant to the change; never commit `build/`,
  `stub_nvm_data/` or tool downloads.
- Commit messages state the resulting behaviour, not the process.
- Do not amend or force-push without being asked.
- Regenerated artifacts belong in their own commit, separate from source changes.
