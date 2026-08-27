import pytest

from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_INCHING_DURATION,
    ZCL_CLUSTER_ON_OFF,
)


@pytest.fixture()
def relay_device(device: Device) -> Device:
    return device


def test_inching_attribute_pulses_relay(relay_device: Device) -> None:
    endpoint = 5
    relay_device.write_zigbee_attr(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 250
    )

    relay_device.zcl_relay_on(endpoint)
    assert relay_device.read_zigbee_attr(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF
    ) == "1"
    relay_device.step_time(249)
    assert relay_device.zcl_relay_get(endpoint) == "1"
    relay_device.step_time(1)
    assert relay_device.zcl_relay_get(endpoint) == "0"


def test_second_inching_on_restarts_deadline(relay_device: Device) -> None:
    endpoint = 5
    relay_device.write_zigbee_attr(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 500
    )
    relay_device.zcl_relay_on(endpoint)
    relay_device.step_time(300)
    relay_device.zcl_relay_on(endpoint)
    relay_device.step_time(499)
    assert relay_device.zcl_relay_get(endpoint) == "1"
    relay_device.step_time(1)
    assert relay_device.zcl_relay_get(endpoint) == "0"


def test_zero_inching_duration_keeps_relay_on(relay_device: Device) -> None:
    endpoint = 5
    relay_device.write_zigbee_attr(
        endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 0
    )
    relay_device.zcl_relay_on(endpoint)
    relay_device.step_time(1000)
    assert relay_device.zcl_relay_get(endpoint) == "1"
