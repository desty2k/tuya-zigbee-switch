"""Cover switch stop button (third X pin) and cover direction LEDs.

The config is the BSEED TS130F (vsj5o7dm) shutter: open B6, close A1, stop A0,
open relay D3, close relay C0, open LED C3, close LED B5, backlight B4.
"""
import pytest

from client import StubProc
from conftest import Device, MINIMUM_SWITCH_TIME_MS
from zcl_consts import (
    ZCL_WINDOW_COVERING_MOVING_CLOSING,
    ZCL_WINDOW_COVERING_MOVING_OPENING,
    ZCL_WINDOW_COVERING_MOVING_STOPPED,
)

CONFIG = "Mfr;Model;LB4;XB6A1A0u;CD3C0;IC3;IB5;M;"
COVER_EP = 2


@pytest.fixture
def shutter() -> Device:
    p = StubProc(device_config=CONFIG).start()
    try:
        d = Device(p)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        yield d
    finally:
        p.stop()


def outputs(d: Device) -> dict[str, int]:
    return {pin: int(d.get_gpio(pin, refresh=True)) for pin in ("D3", "C0", "C3", "B5")}


def test_stop_button_stops_an_opening_cover(shutter: Device):
    shutter.click_button("B6")
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_OPENING
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)

    shutter.click_button("A0")
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert outputs(shutter)["D3"] == 0 and outputs(shutter)["C0"] == 0


def test_stop_button_stops_a_closing_cover(shutter: Device):
    shutter.click_button("A1")
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_CLOSING
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)

    shutter.click_button("A0")
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED


def test_stop_button_on_a_stopped_cover_starts_nothing(shutter: Device):
    shutter.click_button("A0")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert outputs(shutter) == {"D3": 0, "C0": 0, "C3": 0, "B5": 0}


def test_direction_leds_follow_the_motion(shutter: Device):
    assert outputs(shutter) == {"D3": 0, "C0": 0, "C3": 0, "B5": 0}

    shutter.click_button("B6")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert outputs(shutter) == {"D3": 1, "C0": 0, "C3": 1, "B5": 0}

    shutter.click_button("A0")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert outputs(shutter) == {"D3": 0, "C0": 0, "C3": 0, "B5": 0}

    shutter.click_button("A1")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert outputs(shutter) == {"D3": 0, "C0": 1, "C3": 0, "B5": 1}

    shutter.click_button("A1")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert outputs(shutter) == {"D3": 0, "C0": 0, "C3": 0, "B5": 0}


def test_two_pin_cover_switch_keeps_working_without_a_stop_button():
    with StubProc(device_config="Mfr;Model;XB6A1u;CD3C0;IC3;IB5;").start() as p:
        d = Device(p)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        d.click_button("B6")
        assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_OPENING
        assert int(d.get_gpio("C3", refresh=True)) == 1
