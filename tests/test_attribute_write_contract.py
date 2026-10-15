"""Attribute writes follow the HAL write contract (src/hal/zigbee.h).

The stub runs VALIDATE, then COMMIT, in which the cluster stores the value;
like the patched Telink SDK, it copies the candidate itself only for a
cluster without a write callback, so a value COMMIT derived stays stored.
"""

from client import StubProc
from conftest import Device
from management_protocol_consts import (
    ZCL_ATTR_DEVICE_CONFIG_TEXT,
    ZCL_CLUSTER_DEVICE_CONFIG,
)
from zcl_consts import (
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE,
)

CONFIG = "M;Write;SA0u;RB0;"
BUTTON = "A0"
RELAY = "B0"


def _relay_after_click(device: Device) -> bool:
    device.click_button(BUTTON)
    device.step_time(1000)
    return device.get_gpio(RELAY, refresh=True)


def test_switch_mode_write_applies_immediately_and_survives_reboot() -> None:
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        device.freeze_time()
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE
        ) == str(ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE)
        # Toggle mode: press and release each flip the relay.
        assert _relay_after_click(device) is False

        device.zcl_switch_mode_set(1, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY)
        # Momentary mode: one click flips the relay once.
        assert _relay_after_click(device) is True

    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        device.freeze_time()
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE
        ) == str(ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY)
        before = device.get_gpio(RELAY, refresh=True)
        assert _relay_after_click(device) is (not before)


def _undivided(proc: StubProc, records: dict[int, bytes]):
    args = " ".join(f"0x{attr:04X}={value.hex()}" for attr, value in records.items())
    return proc.exec(
        f"zcl_write_undivided 1 0x{ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG:04X} {args}"
    )


def test_undivided_write_with_one_invalid_record_changes_nothing() -> None:
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        res = _undivided(
            proc,
            {
                ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE: bytes([1]),
                ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX: bytes([9]),
            },
        )
        assert not res.ok
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE
        ) == str(ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE)


def test_undivided_write_applies_every_valid_record() -> None:
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        res = _undivided(
            proc,
            {
                ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE: bytes([1]),
                ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR: (1500).to_bytes(2, "little"),
            },
        )
        assert res.ok, res.payload
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE
        ) == str(ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY)
        assert device.read_zigbee_attr(
            1,
            ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
            ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR,
        ) == "1500"


def test_string_longer_than_attribute_storage_is_rejected() -> None:
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        before = device.read_zigbee_attr(
            1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_TEXT
        )
        text = b"M;Long;" + b"x" * 193
        value = len(text).to_bytes(2, "little") + text
        res = proc.exec(
            f"zcl_write_raw 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} {value.hex()}"
        )
        assert not res.ok
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_TEXT
        ) == before


def test_long_string_prefix_past_the_frame_is_rejected_unchanged() -> None:
    """0xFFFD wraps the Telink parser's 16-bit allocation; the frame check stops it."""
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        before = device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_TEXT)
        for value in ("FDFF4D3B", "FFFF4D", "05"):
            res = proc.exec(
                f"zcl_write_raw 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
                f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} {value}"
            )
            assert not res.ok, value
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_TEXT
        ) == before


def test_fixed_size_value_of_wrong_length_is_rejected() -> None:
    with StubProc(device_config=CONFIG) as proc:
        res = proc.exec(
            f"zcl_write_raw 1 0x{ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG:04X} "
            f"0x{ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR:04X} 01"
        )
        assert not res.ok


def test_undivided_cover_write_with_out_of_range_deadzone_changes_nothing() -> None:
    """Cover range checks run in VALIDATE, before any record is committed."""
    with StubProc(device_config="X;Y;CA0A1;") as proc:
        device = Device(proc)
        open_time = device.read_zigbee_attr(1, 0x0102, 0xFF02)
        res = proc.exec(
            "zcl_write_undivided 1 0x0102 0xFF02=30750000 0xFF05=1027"
        )
        assert not res.ok
        assert device.read_zigbee_attr(1, 0x0102, 0xFF02) == open_time
        assert device.read_zigbee_attr(1, 0x0102, 0xFF05) == "0"
