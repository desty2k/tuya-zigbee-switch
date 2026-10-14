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
