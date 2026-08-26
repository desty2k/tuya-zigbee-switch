from client import StubProc
from conftest import Device, wait_for
from tests.zcl_consts import (
    ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT,
    ZCL_CLUSTER_BASIC,
)

HAL_ZIGBEE_NETWORK_JOINED = 1


def test_ten_click_gesture_resets_immediately() -> None:
    with StubProc(device_config="A;B;SA0u;RB1;") as proc:
        device = Device(proc)
        device.click_n("A0", 10)

        wait_for(lambda: any(event.kind == "gesture" for event in device._events))
        gestures = [event for event in device._events if event.kind == "gesture"]
        assert gestures[-1].payload["type"] == "N_CLICK"
        assert gestures[-1].payload["count"] == "10"
        assert device.status()["joined"] != str(HAL_ZIGBEE_NETWORK_JOINED)


def test_onboard_hold_gesture_resets() -> None:
    with StubProc(device_config="A;B;BA0u;") as proc:
        device = Device(proc)
        device.press_button("A0")
        device.step_time(2000)

        wait_for(lambda: any(event.kind == "gesture" for event in device._events))
        gestures = [event for event in device._events if event.kind == "gesture"]
        assert any(event.payload["type"] == "HOLD_START" for event in gestures)
        assert device.status()["joined"] != str(HAL_ZIGBEE_NETWORK_JOINED)


def test_zero_reset_count_disables_gesture_reset() -> None:
    with StubProc(device_config="A;B;SA0u;RB1;") as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT, 0
        )
        device.click_n("A0", 10)

        assert device.status()["joined"] == str(HAL_ZIGBEE_NETWORK_JOINED)
