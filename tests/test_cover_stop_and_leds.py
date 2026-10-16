"""Cover switch stop button (third X pin), cover direction LEDs, and presses
towards the end the cover already rests at.

The config is the BSEED TS130F (vsj5o7dm) shutter: open B6, close A1, stop A0,
open relay D3, close relay C0, open LED C3, close LED B5, backlight B4.
"""
import pytest

from client import StubProc
from conftest import Device, MINIMUM_SWITCH_TIME_MS
from zcl_consts import (
    ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME,
    ZCL_ATTR_WINDOW_COVERING_OPEN_TIME,
    ZCL_ATTR_WINDOW_COVERING_OVERRUN,
    ZCL_CLUSTER_WINDOW_COVERING,
    ZCL_WINDOW_COVERING_MOVING_CLOSING,
    ZCL_WINDOW_COVERING_MOVING_OPENING,
    ZCL_WINDOW_COVERING_MOVING_STOPPED,
)

CONFIG = "Mfr;Model;LB4;XB6A1A0u;CD3C0;IC3;IB5;M;"
COVER_EP = 2
# The owner's shutter: 18 s up and down, no overrun.
TRAVEL_MS = 18_000
# Lift percentage: 0 open, 100 closed.
CLOSED = 100


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


def calibrate(d: Device, overrun_ms: int) -> None:
    for attr, value in ((ZCL_ATTR_WINDOW_COVERING_OPEN_TIME, TRAVEL_MS),
                        (ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME, TRAVEL_MS),
                        (ZCL_ATTR_WINDOW_COVERING_OVERRUN, overrun_ms)):
        d.write_zigbee_attr(COVER_EP, ZCL_CLUSTER_WINDOW_COVERING, attr, value)


def relay_switched_on(d: Device, pin: str) -> int:
    """Times the pin was driven high since the last clear_events()."""
    pin_id = d._parse_pin(pin)
    return sum(1 for evt in d._events
               if evt.kind == "gpio" and int(evt.payload.get("pin", "-1")) == pin_id
               and evt.payload.get("value") == "1")


def run_to_end(d: Device, button: str, overrun_ms: int) -> None:
    d.click_button(button)
    d.step_time(TRAVEL_MS + overrun_ms + MINIMUM_SWITCH_TIME_MS)
    assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED


@pytest.mark.parametrize(("button", "relay", "position"), [("A1", "C0", 100), ("B6", "D3", 0)])
def test_press_towards_a_known_end_leaves_the_relay_off(shutter: Device, button: str,
                                                        relay: str, position: int):
    calibrate(shutter, 0)
    run_to_end(shutter, button, 0)
    run_to_end(shutter, button, 0)
    assert shutter.zcl_cover_get_position(COVER_EP) == position
    shutter.clear_events()

    shutter.click_button(button)
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relay_switched_on(shutter, relay) == 0
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED

    shutter.zcl_cover_goto(COVER_EP, position)
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relay_switched_on(shutter, relay) == 0


def test_press_towards_a_known_end_ignores_the_overrun(shutter: Device):
    calibrate(shutter, 500)
    run_to_end(shutter, "A1", 500)
    shutter.clear_events()

    shutter.click_button("A1")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relay_switched_on(shutter, "C0") == 0


def test_press_after_a_stop_at_the_end_runs_the_overrun(shutter: Device):
    calibrate(shutter, 2000)
    run_to_end(shutter, "B6", 2000)
    shutter.click_button("A1")
    shutter.step_time(TRAVEL_MS + 1000)
    shutter.click_button("A0")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert shutter.zcl_cover_get_position(COVER_EP) == CLOSED
    shutter.clear_events()

    shutter.click_button("A1")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relay_switched_on(shutter, "C0") == 1
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_CLOSING
    shutter.step_time(2000)
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED


def test_press_from_an_unknown_position_runs_the_full_travel(shutter: Device):
    calibrate(shutter, 0)
    shutter.clear_events()

    shutter.click_button("A1")
    shutter.step_time(TRAVEL_MS - 100)
    assert relay_switched_on(shutter, "C0") == 1
    assert shutter.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_CLOSING
