import queue

from tests.client import StubProc
from tests.management_protocol_consts import (
    ZCL_ATTR_DEVICE_CONFIG_TEXT,
    ZCL_CLUSTER_DEVICE_CONFIG,
)

READ_CONFIG = (
    f"zcl_read 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
    f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X}"
)


def assert_no_pending_response(process: StubProc) -> None:
    try:
        stray = process._res_q.get(timeout=0.2)
    except queue.Empty:
        return
    raise AssertionError(f"unexpected extra response: {stray}")


def test_long_command_is_dispatched_as_one_line() -> None:
    with StubProc(device_config="A;B;") as process:
        result = process.exec("x" * 600)
        assert not result.ok
        assert result.payload == {"unknown_cmd": "x" * 600}
        assert_no_pending_response(process)
        assert process.exec(READ_CONFIG).payload["value"] == "A;B;"


def test_over_long_command_is_rejected_without_desynchronizing() -> None:
    with StubProc(device_config="A;B;") as process:
        result = process.exec("zcl_read " + "1" * 3000)
        assert not result.ok
        assert "line_too_long" in result.payload
        assert_no_pending_response(process)
        assert process.exec(READ_CONFIG).payload["value"] == "A;B;"
