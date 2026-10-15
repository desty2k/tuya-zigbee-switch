import pytest

from tests.client import StubProc
from tests.conftest import DEBOUNCE_MS, Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_INCHING_DURATION,
    ZCL_ATTR_START_UP_ONOFF,
    ZCL_CLUSTER_LEVEL_CONTROL,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
    ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE,
    ZCL_CMD_ONOFF_TOGGLE,
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


INCHING_CONFIG = "X;Y;SA0u;RB0;"
INCHING_SWITCH_PIN = "A0"
INCHING_RELAY_EP = 2
INCHING_MS = 300


def start_inching_device(proc: StubProc) -> Device:
    device = Device(proc)
    device.write_zigbee_attr(
        INCHING_RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION,
        INCHING_MS,
    )
    return device


def assert_pulses(device: Device) -> None:
    assert device.zcl_relay_get(INCHING_RELAY_EP) == "1"
    device.step_time(INCHING_MS - 1)
    assert device.zcl_relay_get(INCHING_RELAY_EP) == "1"
    device.step_time(1)
    assert device.zcl_relay_get(INCHING_RELAY_EP) == "0"


@pytest.mark.parametrize(
    ("cluster", "command", "payload"),
    [
        (ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE, None),
        (ZCL_CLUSTER_ON_OFF, ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE, None),
        (ZCL_CLUSTER_LEVEL_CONTROL, ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
         bytes((0xFE, 0, 0))),
    ],
    ids=["toggle", "on_with_recall", "move_to_level_with_on_off"],
)
def test_inching_applies_to_every_zigbee_on_command(
    cluster: int, command: int, payload: bytes | None
) -> None:
    with StubProc(device_config=INCHING_CONFIG) as proc:
        device = start_inching_device(proc)
        device.call_zigbee_cmd(INCHING_RELAY_EP, cluster, command, payload)
        assert_pulses(device)


def test_inching_applies_to_local_switch() -> None:
    with StubProc(device_config=INCHING_CONFIG) as proc:
        device = start_inching_device(proc)
        # press_button steps past the debounce, so the pulse started up to
        # one debounce interval before it returns.
        device.press_button(INCHING_SWITCH_PIN)
        assert device.zcl_relay_get(INCHING_RELAY_EP) == "1"
        device.step_time(INCHING_MS - 2 * DEBOUNCE_MS)
        assert device.zcl_relay_get(INCHING_RELAY_EP) == "1"
        device.step_time(2 * DEBOUNCE_MS)
        assert device.zcl_relay_get(INCHING_RELAY_EP) == "0"


def test_inching_applies_to_startup_on() -> None:
    with StubProc(device_config=INCHING_CONFIG) as proc:
        device = start_inching_device(proc)
        device.write_zigbee_attr(
            INCHING_RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_START_UP_ONOFF, 1
        )
    with StubProc(device_config=INCHING_CONFIG) as proc:
        assert_pulses(Device(proc))
