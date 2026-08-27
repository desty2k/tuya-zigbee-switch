from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_CMD_ON_WITH_TIMED_OFF,
    ZCL_CLUSTER_ON_OFF,
)


def timed_off_payload(on_time_ds: int) -> bytes:
    return bytes((0, on_time_ds & 0xFF, on_time_ds >> 8, 0, 0))


def test_on_with_timed_off_expires(device: Device) -> None:
    endpoint = 5
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(300),
    )
    device.step_time(29999)
    assert device.zcl_relay_get(endpoint) == "1"
    device.step_time(1)
    assert device.zcl_relay_get(endpoint) == "0"


def test_on_with_timed_off_replaces_deadline(device: Device) -> None:
    endpoint = 5
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(50),
    )
    device.step_time(3000)
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(50),
    )
    device.step_time(4999)
    assert device.zcl_relay_get(endpoint) == "1"
    device.step_time(1)
    assert device.zcl_relay_get(endpoint) == "0"


def test_explicit_off_cancels_timed_off(device: Device) -> None:
    endpoint = 5
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(50),
    )
    device.step_time(1000)
    device.zcl_relay_off(endpoint)
    device.step_time(5000)
    assert device.zcl_relay_get(endpoint) == "0"


def test_on_with_timed_off_rejects_short_payload(device: Device) -> None:
    result = device.call_zigbee_cmd_raw(
        5, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF, b"\x00\x01"
    )
    assert result["result"] == "MALFORMED_COMMAND"
