"""Raw ZCL foundation frames pass the frame check before any record is parsed.

`zcl_foundation` injects a foundation command payload the way the patched
Telink zcl_foundationCmdHandler() receives it, so these cases fail when the
check in src/zigbee/zcl_frame_check.h is bypassed or too lax.
"""

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE,
)

CONFIG = "M;Frames;SA0u;RB0;"

ZCL_CMD_WRITE = 0x02
ZCL_CMD_CONFIG_REPORT = 0x06
ZCL_CMD_READ_REPORT_CFG = 0x08
ZCL_DATA_TYPE_ENUM8 = 0x30
ZCL_DIRECTION_RECEIVE_REPORTS = 0x01


def _foundation(proc: StubProc, cluster: int, command: int, payload: bytes) -> str:
    res = proc.exec(f"zcl_foundation 1 0x{cluster:04X} 0x{command:02X} {payload.hex()}")
    assert res.ok, res
    return res.payload["result"]


def _read_report_cfg(records: int) -> bytes:
    # direction, attribute id
    return b"".join(bytes([0x00]) + ZCL_ATTR_ONOFF.to_bytes(2, "little") for _ in range(records))


def _config_report(records: int) -> bytes:
    # receive direction: direction, attribute id, timeout
    record = bytes([ZCL_DIRECTION_RECEIVE_REPORTS]) + ZCL_ATTR_ONOFF.to_bytes(2, "little")
    return (record + (60).to_bytes(2, "little")) * records


def test_read_reporting_configuration_beyond_the_response_buffer_is_malformed() -> None:
    """The SDK sizes the response in a u8: 18 records wrap it to 15 bytes."""
    with StubProc(device_config=CONFIG) as proc:
        assert _foundation(proc, ZCL_CLUSTER_ON_OFF, ZCL_CMD_READ_REPORT_CFG,
                           _read_report_cfg(16)) == "PROCESSED"
        for records in (17, 18):
            assert _foundation(proc, ZCL_CLUSTER_ON_OFF, ZCL_CMD_READ_REPORT_CFG,
                               _read_report_cfg(records)) == "MALFORMED_COMMAND", records
        assert _foundation(proc, ZCL_CLUSTER_ON_OFF, ZCL_CMD_READ_REPORT_CFG,
                           _read_report_cfg(1) + b"\x00") == "MALFORMED_COMMAND"


def test_configure_reporting_beyond_the_parse_buffer_is_malformed() -> None:
    """The SDK sizes the parsed command in a u8: 18 records fit, 19 do not."""
    with StubProc(device_config=CONFIG) as proc:
        assert _foundation(proc, ZCL_CLUSTER_ON_OFF, ZCL_CMD_CONFIG_REPORT,
                           _config_report(18)) == "PROCESSED"
        assert _foundation(proc, ZCL_CLUSTER_ON_OFF, ZCL_CMD_CONFIG_REPORT,
                           _config_report(19)) == "MALFORMED_COMMAND"


def test_truncated_write_record_is_malformed_and_writes_nothing() -> None:
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        mode = ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE.to_bytes(2, "little")
        whole = mode + bytes([ZCL_DATA_TYPE_ENUM8, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY])

        assert _foundation(proc, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_CMD_WRITE,
                           whole + mode + bytes([ZCL_DATA_TYPE_ENUM8])) == "MALFORMED_COMMAND"
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE
        ) == str(ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE)

        assert _foundation(proc, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_CMD_WRITE,
                           whole) == "PROCESSED"
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE
        ) == str(ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY)
