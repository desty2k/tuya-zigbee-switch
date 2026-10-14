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
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_MODE,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
    ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
NVM_DIR = Path("./stub_nvm_data")
NV_ITEM_CURRENT_VERSION_IN_NV = 0x01
NV_ITEM_DEVICE_TYPE = 0x20

CONFIG = "A;B;SA0u;RB0;"
SWITCH_EP = 1
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


def relay_mode(device: Device) -> str:
    return device.read_zigbee_attr(SWITCH_EP, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
                                   ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_MODE)


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
        device.zcl_switch_relay_mode_set(SWITCH_EP,
                                         ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED)
        assert process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET:04X} 3"
        ).ok
        assert relay_mode(device) == str(ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED)
        assert multi_press_reset(device) == "3"
    device_type_record = nvm_item(NV_ITEM_DEVICE_TYPE).read_bytes()

    # Same NVM as a device running firmware with migration version 1.
    nvm_item(NV_ITEM_CURRENT_VERSION_IN_NV).write_bytes(struct.pack("<H", 1))

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

    # Reapplying the layout shows the switch and relay owners at defaults.
    with StubProc(device_config=CONFIG) as process:
        device = Device(process)
        assert relay_mode(device) != str(ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED)
        assert multi_press_reset(device) == "10"
    assert stored_version() == migrations_version()
