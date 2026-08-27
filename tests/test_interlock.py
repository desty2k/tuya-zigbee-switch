from collections.abc import Iterator

import pytest

from tests.client import StubProc
from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF_INTERLOCK_GROUP,
    ZCL_ATTR_ONOFF_INCHING_DURATION,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CMD_ONOFF_TOGGLE,
    ZCL_CMD_ON_WITH_TIMED_OFF,
)


def start_device(config: str) -> Iterator[Device]:
    with StubProc(device_config=config) as proc:
        yield Device(proc)


@pytest.fixture()
def grouped_relays() -> Iterator[Device]:
    yield from start_device("X;Y;RA0;RA1;RA2;K07;")


def timed_off_payload(on_time_ds: int) -> bytes:
    return bytes((0, on_time_ds & 0xFF, on_time_ds >> 8, 0, 0))


def test_zigbee_on_and_toggle_keep_one_group_member_on(
    grouped_relays: Device,
) -> None:
    grouped_relays.zcl_relay_on(1)
    grouped_relays.zcl_relay_on(2)
    assert grouped_relays.zcl_relay_get(1) == "0"
    assert grouped_relays.zcl_relay_get(2) == "1"

    grouped_relays.call_zigbee_cmd(3, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE)
    assert grouped_relays.zcl_relay_get(2) == "0"
    assert grouped_relays.zcl_relay_get(3) == "1"


def test_inching_and_timed_on_respect_group(grouped_relays: Device) -> None:
    grouped_relays.zcl_relay_on(1)
    grouped_relays.write_zigbee_attr(
        2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 500
    )
    grouped_relays.zcl_relay_on(2)
    assert grouped_relays.zcl_relay_get(1) == "0"
    assert grouped_relays.zcl_relay_get(2) == "1"
    grouped_relays.step_time(500)
    assert grouped_relays.zcl_relay_get(2) == "0"

    grouped_relays.zcl_relay_on(1)
    grouped_relays.call_zigbee_cmd(
        3,
        ZCL_CLUSTER_ON_OFF,
        ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(10),
    )
    assert grouped_relays.zcl_relay_get(1) == "0"
    assert grouped_relays.zcl_relay_get(3) == "1"
    grouped_relays.step_time(1000)
    assert grouped_relays.zcl_relay_get(3) == "0"


def test_button_requests_respect_group() -> None:
    with StubProc(device_config="X;Y;SA0u;SA1u;RA2;RA3;K03;") as proc:
        device = Device(proc)
        device.press_button("A0")
        assert device.zcl_relay_get(3) == "1"
        device.press_button("A1")
        assert device.zcl_relay_get(3) == "0"
        assert device.zcl_relay_get(4) == "1"


def test_interlock_attribute_builds_dynamic_group() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;") as proc:
        device = Device(proc)
        for endpoint in (1, 2):
            device.write_zigbee_attr(
                endpoint,
                ZCL_CLUSTER_ON_OFF,
                ZCL_ATTR_ONOFF_INTERLOCK_GROUP,
                4,
            )
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(2) == "1"


def test_dead_time_defers_new_target_and_replaces_pending() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;RA2;K07:50;") as proc:
        device = Device(proc)
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(2) == "0"
        device.step_time(20)
        device.zcl_relay_on(3)
        device.step_time(49)
        assert device.zcl_relay_get(2) == "0"
        assert device.zcl_relay_get(3) == "0"
        device.step_time(1)
        assert device.zcl_relay_get(2) == "0"
        assert device.zcl_relay_get(3) == "1"


def test_off_cancels_pending_target() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;K03:50;") as proc:
        device = Device(proc)
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        device.zcl_relay_off(2)
        device.step_time(50)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(2) == "0"


def test_deferred_inching_starts_duration_after_dead_time() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;K03:50;") as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 500
        )
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        device.step_time(50)
        assert device.zcl_relay_get(2) == "1"
        device.step_time(499)
        assert device.zcl_relay_get(2) == "1"
        device.step_time(1)
        assert device.zcl_relay_get(2) == "0"
