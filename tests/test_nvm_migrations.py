import re
import struct
from pathlib import Path

from tests.client import Event, StubProc
from tests.conftest import Device
from tests.management_protocol_consts import (
    ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET,
    ZCL_ATTR_DEVICE_CONFIG_TEXT,
    ZCL_CLUSTER_DEVICE_CONFIG,
)
from tests.zcl_consts import (
    ZCL_ATTR_COVER_SWITCH_LOCAL_MODE,
    ZCL_ATTR_COVER_SWITCH_REVERSAL,
    ZCL_ATTR_COVER_SWITCH_SWITCH_TYPE,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_BINDING_MODE,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_MODE,
    ZCL_ATTR_ONOFF_INCHING_DURATION,
    ZCL_ATTR_ONOFF_INDICATOR_MODE,
    ZCL_ATTR_START_UP_ONOFF,
    ZCL_ATTR_WINDOW_COVERING_MOTOR_REVERSAL,
    ZCL_ATTR_WINDOW_COVERING_OPEN_TIME,
    ZCL_CLUSTER_COVER_SWITCH_CONFIG,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
    ZCL_CLUSTER_WINDOW_COVERING,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
NVM_DIR = Path("./stub_nvm_data")
NV_ITEM_CURRENT_VERSION_IN_NV = 0x01
NV_ITEM_DEVICE_TYPE = 0x20

# Endpoints: 1 switch, 2 relay with indicator, 3 cover switch, 4 cover.
CONFIG = "A;B;SA0u;RB0;IC0;XA2A3u;CB1B2;"
SWITCH_EP = 1
RELAY_EP = 2
COVER_SWITCH_EP = 3
COVER_EP = 4

# (endpoint, cluster, attribute, non-default value) for every persisted
# switch, relay, indicator, cover-switch and cover setting.
OWNED_SETTINGS = [
    (SWITCH_EP, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
     ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE, 1),
    (SWITCH_EP, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
     ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_MODE, 0),
    (SWITCH_EP, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
     ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR, 1500),
    (SWITCH_EP, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
     ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_BINDING_MODE, 0),
    (RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_START_UP_ONOFF, 1),
    (RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, 1),
    (RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 500),
    (COVER_SWITCH_EP, ZCL_CLUSTER_COVER_SWITCH_CONFIG,
     ZCL_ATTR_COVER_SWITCH_SWITCH_TYPE, 0),
    (COVER_SWITCH_EP, ZCL_CLUSTER_COVER_SWITCH_CONFIG,
     ZCL_ATTR_COVER_SWITCH_LOCAL_MODE, 0),
    (COVER_SWITCH_EP, ZCL_CLUSTER_COVER_SWITCH_CONFIG,
     ZCL_ATTR_COVER_SWITCH_REVERSAL, 1),
    (COVER_EP, ZCL_CLUSTER_WINDOW_COVERING, ZCL_ATTR_WINDOW_COVERING_MOTOR_REVERSAL, 1),
    (COVER_EP, ZCL_CLUSTER_WINDOW_COVERING, ZCL_ATTR_WINDOW_COVERING_OPEN_TIME, 20000),
]

# Item ids of the per-cluster records written by firmware before the
# capability-state layout: switch 4-8, relay 9-13, cover switch 14-16, plus
# the basic-cluster record (3) and the standalone multi-press count (33).
# Current firmware never reads these ids, so only their deletion is
# observable.
OLD_LAYOUT_ITEMS = [0x03, *range(0x04, 0x09), *range(0x09, 0x0E),
                    *range(0x0E, 0x11), 0x21]
STUB_DEFAULT_CONFIG = "unknown;TS0012-CUSTOM;"


def migrations_version() -> int:
    return int((REPO_ROOT / "NVM_MIGRATIONS_VERSION").read_text().strip())


def nvm_item(item_id: int) -> Path:
    return NVM_DIR / f"item_{item_id:02x}.bin"


def stored_version() -> int:
    return struct.unpack("<H", nvm_item(NV_ITEM_CURRENT_VERSION_IN_NV).read_bytes())[0]


def multi_press_reset(device: Device) -> str:
    return device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                   ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET)


def read_settings(device: Device) -> list[str]:
    return [device.read_zigbee_attr(ep, cluster, attr)
            for ep, cluster, attr, _ in OWNED_SETTINGS]


def test_stub_stores_version_from_migrations_version_file() -> None:
    with StubProc(device_config=CONFIG):
        pass
    assert stored_version() == migrations_version()


def test_silabs_config_default_matches_migrations_version_file() -> None:
    header = (REPO_ROOT / "src/silabs/config/silabs_config.h").read_text()
    match = re.search(r"#define\s+NVM_MIGRATIONS_VERSION\s+(\d+)", header)
    assert match is not None
    assert int(match.group(1)) == migrations_version()


def test_boot_from_older_migration_version_resets_settings_and_stays_joined() -> None:
    with StubProc(device_config=CONFIG) as process:
        device = Device(process)
        defaults = read_settings(device)
        for (ep, cluster, attr, value), default in zip(OWNED_SETTINGS, defaults):
            assert str(value) != default, (ep, cluster, attr)
            device.write_zigbee_attr(ep, cluster, attr, value)
        assert process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET:04X} 3"
        ).ok
        assert read_settings(device) == [str(v) for *_, v in OWNED_SETTINGS]
    with StubProc() as process:
        assert read_settings(Device(process)) == [str(v) for *_, v in OWNED_SETTINGS]
    device_type_record = nvm_item(NV_ITEM_DEVICE_TYPE).read_bytes()

    # Same NVM as a device running firmware with migration version 1.
    nvm_item(NV_ITEM_CURRENT_VERSION_IN_NV).write_bytes(struct.pack("<H", 1))
    for item_id in OLD_LAYOUT_ITEMS:
        nvm_item(item_id).write_bytes(bytes([0x5A]) * 8)

    events: list[Event] = []
    with StubProc() as process:
        process.on_event.append(events.append)
        device = Device(process)
        device.step_time(5000)
        assert process.is_running()
        assert device.is_joined()
        assert not any(evt.kind == "zcl_leave_network" for evt in events)
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_TEXT) == STUB_DEFAULT_CONFIG
        assert multi_press_reset(device) == "10"
    assert stored_version() == migrations_version()
    assert nvm_item(NV_ITEM_DEVICE_TYPE).read_bytes() == device_type_record
    assert [item_id for item_id in OLD_LAYOUT_ITEMS if nvm_item(item_id).exists()] == []

    # Reapplying the layout shows every switch, relay and cover owner at defaults.
    with StubProc(device_config=CONFIG) as process:
        device = Device(process)
        assert read_settings(device) == defaults
        assert multi_press_reset(device) == "10"
    assert stored_version() == migrations_version()
