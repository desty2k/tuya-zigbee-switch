# Tests

Two suites run in CI (`.github/workflows/test.yml`) next to
`make tools/check_device_features`:

| Suite | Command | What it runs |
| --- | --- | --- |
| Native unit tests | `make unit_tests` | Single modules compiled for the host with fakes from `tests/unit/support/`, plus the parse check of every board's config string |
| Stub tests | `make tests` | pytest in `tests/` driving the host build of the whole firmware (`src/stub/`) over its REPL |

Install the Python dependencies once (`make setup_venv`, or
`pip install -r requirements.txt`).

## Native unit tests (`tests/unit/`)

`make unit_tests` builds each test from `tests/unit/test_<module>.c` and the
sources it needs into `build/unit/`, with `-Wall -Wextra -Werror -std=c99
-DHAL_STUB` and every feature enabled, and runs them.

| Test | Covers |
| --- | --- |
| `test_gpio_edge_queue`, `test_button_input`, `fuzz_debounce` | Edge ring, debounce, overflow resync |
| `test_gesture_fsm`, `test_action_mapper` | Holds, multi-clicks, reset triggers |
| `test_timer_service` | Timer arm, cancel, failed arm |
| `test_relay_controller`, `test_interlock`, `test_cover_controller` | Relay requests, interlock dead time, cover motion |
| `test_button_event_queue` | Button event queue, pacing, time to live |
| `test_config_parser`, `test_config_parser_features` | Config tokens; the feature test is built twice, as `test_config_parser_switch_only` (`relay switch`) and `test_config_parser_cover_battery` (`cover battery interlock`) |
| `test_capability_state` | Persisted per-capability records |
| `test_zigbee_descriptor`, `test_telink_zigbee_init` | Descriptor limits; Telink registration against `tests/unit/telink_sdk_stub/` |
| `test_gpio_counter`, `test_bl0937`, `test_meter_service`, `test_fixed_math` | Meter path |
| `test_gpio_probe_service` | GPIO probe safety rules |
| `test_gpio_pin_mapping` | Pin name tables of both platforms |
| `check_device_db_configs` | Reads `python3 helper_scripts/board_features.py configs` on stdin and parses every `config_str` and `alt_config_str` of every built board with the firmware parser. A rejected `config_str` fails; a rejected alternative is only reported |

Support code in `tests/unit/support/`: `runner.h` (`TEST`, `ASSERT_*`),
`fake_clock` (`hal_millis`), `fake_tasks` (`hal_tasks`, with
`fake_tasks_poll()` and `fake_tasks_reject_next()` to simulate a full timer
pool), `fake_gpio`, `fake_gpio_counter`, `fake_gpio_parse`, `fake_relay_hw`.

A new unit test needs a target in the `Makefile` (sources listed explicitly),
an entry in the `unit_tests` prerequisites and a line that runs it.

## Stub tests (`tests/`)

### Stub binaries

`make tests` builds four stub variants (`src/stub/Makefile`) into
`build/stub/`, then runs `python -m pytest tests/ -v`:

| Target | Binary | Image |
| --- | --- | --- |
| `make stub/build` | `stub_device` | Router, every default feature |
| `make stub/build_end_device` | `stub_end_device` | End device (`-DEND_DEVICE`) |
| `make stub/build_switch_only` | `stub_switch_only` | `relay switch` only |
| `make stub/build_probe` | `stub_probe` | `probe relay switch interlock` |

Tests pick a variant with `StubProc(cmd=["./build/stub/<binary>"], ...)`;
the default is `stub_device`.

Command line options of the stub:

| Option | Effect |
| --- | --- |
| `--device-config <string>` | Config string used when NVM holds none |
| `--not-joined` | Start outside a network |
| `--freeze-time` | Virtual time advances only on `step_time` / `time_advance` |
| `--fail-zigbee-init` | Refuse the next Zigbee init (recovery path tests) |

### Virtual time

`StubProc` starts the stub with `--freeze-time` by default. Time then moves
only when a test steps it, and due tasks run during the step, so timing tests
are deterministic. Never wait on firmware timers with wall-clock sleeps.

### Test client

`tests/client.py`:

- `StubProc(cmd=..., joined=True, freeze_time=True, device_config=None)`
  starts the binary, switches it to machine mode (`machine on`) and reads its
  output on a thread. Use it as a context manager or call `start()` / `stop()`.
- `exec(cmd, timeout=1.0)` sends a REPL command and returns a `CmdResult`
  (`ok`, `payload` as key/value pairs) from the `RES OK|ERR` line.
- `on_event` callbacks receive `Event`s from `EVT <name> ...` lines.

`tests/conftest.py`:

| Fixture / helper | Purpose |
| --- | --- |
| `cleanup_nvm` (autouse) | Deletes `stub_nvm_data/` before and after every test |
| `device_config` | Config string of the test; override it in a module. Default: one LED, four switches, four relays |
| `button_pins`, `relay_pins`, `led_pin`, `relay_button_pairs` | Pins derived from `device_config` |
| `stub_proc`, `device` | A started `StubProc` and the `Device` wrapper around it |
| `Device` GPIO | `set_gpio`, `get_gpio`, `pin_state`, `press_button`, `release_button`, `click_button`, `long_click_button`, `click_n`, `hold_button`, `pin_edge`, `pin_burst`, `pin_late`, `inject_edges` |
| `Device` time | `freeze_time`, `unfreeze_time`, `step_time`, `time_advance`, `tasks_poll`, `time_model`, `time_set`, `millis` |
| `Device` Zigbee | `read_zigbee_attr`, `write_zigbee_attr`, `call_zigbee_cmd`, `call_zigbee_cmd_no_activity` (and `_raw` variants), `zcl_list_cmds`, `wait_for_attr_change`, `wait_for_cmd_send`, `wait_for_announce`, `set_network`, `is_joined`, `poll_rate_ms` |
| `Device` cluster shortcuts | `zcl_relay_*`, `zcl_switch_*`, `zcl_cover_switch_*`, `zcl_cover_*` |
| `Device` diagnostics | `counters()` (`diag`), `input_diag()`, `task_fail`, `set_battery_voltage`, `status` |
| `wait_for`, `wait_not_none`, `ensure_never_true` | Polling helpers for asynchronous output |

ZCL ids come from `tests/zcl_consts.py`; management cluster ids from the
generated `tests/management_protocol_consts.py`.

### REPL commands

Defined in the command table at the end of `src/stub/commands.c`. Run
`make stub/run` (or `./build/stub/stub_device --device-config "<string>"`) to
use them interactively; `help` lists them.

| Area | Commands |
| --- | --- |
| Session | `machine on\|off`, `help`, `status` / `s`, `quit` / `q` |
| GPIO | `set_pin <pin> <0\|1>`, `read_pin <pin>`, `pin_state <pin>`, `pin_edge <pin> <0\|1> [after_ms]`, `pin_burst`, `pin_late`, `pulse_rate <pin> <mHz> [gate pin level]` |
| Time and tasks | `freeze_time <0\|1>`, `step_time <ms>`, `time_advance <ms>`, `tasks_poll`, `time_model native\|telink`, `time_set <virtual_ms>`, `task_fail <count>` |
| Zigbee | `zcl_read`, `zcl_write`, `zcl_write_raw`, `zcl_write_undivided`, `zcl_attr_callback`, `zcl_cmd`, `zcl_cmd_no_activity`, `zcl_foundation`, `zcl_configure_reporting`, `zcl_list_attrs`, `zcl_clusters`, `zcl_attrs`, `zcl_graph_usage`, `net <0\|1\|2>` |
| Fault injection | `tx_fail <count>`, `announce_fail <count>`, `nvm_fail <count>`, `task_fail <count>` |
| Diagnostics | `diag`, `input_diag` |
| Other | `set_battery_voltage <mV>`; `probe_pin`, `probe_pin_info` in probe builds |

Pins are numeric in the REPL; `Device` converts names such as `B1`.

Machine events: `btn_event`, `gesture`, `gpio`, `zcl_attr_change`,
`zcl_attr_report`, `zcl_cmd_send`, `zcl_init_failed`, `zcl_leave_network`,
`zcl_start_network_steering`, `zdo_announce`.

### Test files

| Area | Files |
| --- | --- |
| Input pipeline | `test_input_timing.py`, `test_input_latency.py`, `test_button_events.py`, `test_gesture_reset.py`, `test_base_components.py` |
| Switches | `test_switch_cluster.py`, `test_switch_cluster_momentary_mode.py`, `test_switch_cluster_toggle_mode.py`, `test_switches_without_relays.py` |
| Relays | `test_relay_cluster.py`, `test_relay_inching.py`, `test_relay_timed_off.py`, `test_latching_relay.py`, `test_interlock.py`, `test_indicator_flash.py` |
| Covers | `test_cover_cluster.py`, `test_cover_switch_cluster.py`, `test_cover_stop_and_leds.py` |
| Child lock | `test_child_lock.py` |
| Device config and composition | `test_device_config_parser.py`, `test_composition_graph.py`, `test_build_features.py`, `test_management_clusters.py` |
| Zigbee behaviour | `test_basic_cluster.py`, `test_attribute_reporting.py`, `test_attribute_write_contract.py`, `test_foundation_frames.py`, `test_network_join.py`, `test_poll_control_cluster.py`, `test_battery_cluster.py` |
| Meter | `test_meter.py` |
| GPIO probe | `test_gpio_probe.py` |
| Persistence | `test_nvm_migrations.py` |
| Integrations and tooling | `test_integration_parity.py` (generated converters vs. the firmware descriptor), `test_helper_scripts.py`, `test_check_tc32_jump_tables.py`, `test_stub_repl.py` |

## Writing a test

```python
import pytest
from conftest import Device
from zcl_consts import ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF


@pytest.fixture
def device_config() -> str:
    return "Mfr;Model;SA0u;RB0;M;"  # M: momentary switch, a click toggles


def test_click_toggles_relay(device: Device) -> None:
    device.click_button("A0")
    assert device.read_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF) == "1"
```

Endpoints are numbered switches first, then relays, cover switches and covers
(see [architecture.md](./architecture.md#composition-and-features)), so the
relay above is endpoint 2. Without `M` a switch defaults to toggle mode (a
latching wall switch), where press and release each act on the relay, so a
click leaves it off. For a variant binary or a custom start, use
`StubProc` directly:

```python
from client import StubProc

with StubProc(cmd=["./build/stub/stub_switch_only"], device_config="M;T;SA0u;RB0;") as proc:
    assert proc.exec("s").ok
```

Run one file with `pytest tests/test_relay_cluster.py -v` after
`make stub/build`.
