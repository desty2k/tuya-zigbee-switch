from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_CMD_ON_WITH_TIMED_OFF,
    ZCL_CLUSTER_ON_OFF,
)


ACCEPT_ONLY_WHEN_ON = 0x01


def timed_off_payload(on_time_ds: int, control: int = 0) -> bytes:
    return bytes((control, on_time_ds & 0xFF, on_time_ds >> 8, 0, 0))


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


def test_accept_only_when_on_ignores_off_relay(device: Device) -> None:
    endpoint = 5
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(50, ACCEPT_ONLY_WHEN_ON),
    )
    assert device.zcl_relay_get(endpoint) == "0"
    device.step_time(6000)
    assert device.zcl_relay_get(endpoint) == "0"


def test_accept_only_when_on_extends_on_relay(device: Device) -> None:
    endpoint = 5
    device.zcl_relay_on(endpoint)
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(50, ACCEPT_ONLY_WHEN_ON),
    )
    device.step_time(4999)
    assert device.zcl_relay_get(endpoint) == "1"
    device.step_time(1)
    assert device.zcl_relay_get(endpoint) == "0"


def test_zero_on_time_does_not_switch_an_off_relay(device: Device) -> None:
    endpoint = 5
    relay_pin = "B0"
    device.get_gpio(relay_pin, refresh=True)
    device.clear_events()
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(0),
    )
    device.step_time(200)
    assert device.zcl_relay_get(endpoint) == "0"
    assert device.get_gpio(relay_pin, refresh=True) is False
    pin = device._parse_pin(relay_pin)
    assert not any(
        evt.kind == "gpio" and int(evt.payload.get("pin", "-1")) == pin
        and evt.payload.get("value") == "1"
        for evt in device._events
    )


def test_zero_on_time_keeps_running_deadline(device: Device) -> None:
    endpoint = 5
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(50),
    )
    device.step_time(1000)
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(0),
    )
    device.step_time(3999)
    assert device.zcl_relay_get(endpoint) == "1"
    device.step_time(1)
    assert device.zcl_relay_get(endpoint) == "0"


def test_shorter_on_time_keeps_longer_deadline(device: Device) -> None:
    endpoint = 5
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(50),
    )
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(10),
    )
    device.step_time(4999)
    assert device.zcl_relay_get(endpoint) == "1"
    device.step_time(1)
    assert device.zcl_relay_get(endpoint) == "0"


def test_maximum_on_time_never_expires(device: Device) -> None:
    endpoint = 5
    device.call_zigbee_cmd(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(0xFFFF),
    )
    device.step_time(0xFFFF * 100 + 1000)
    assert device.zcl_relay_get(endpoint) == "1"
