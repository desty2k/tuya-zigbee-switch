"""Device child lock: lock gestures, ignored local inputs, LED feedback.

Holding open and close of a cover switch, or the first two switch buttons,
for 3 s toggles the lock; a single switch button is held for 10 s. While
locked, presses that start do nothing locally or towards bindings and blink
the lock LED once. Locking blinks 3 times, unlocking twice. Remote control
and the Device Config child lock attribute keep working.
"""
from collections.abc import Iterator

import pytest

from client import StubProc
from conftest import Device, MINIMUM_SWITCH_TIME_MS
from zcl_consts import (
    ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK,
    ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR,
    ZCL_ATTR_DEVICE_CONFIG_TEXT,
    ZCL_CLUSTER_DEVICE_CONFIG,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CLUSTER_WINDOW_COVERING,
    ZCL_CMD_ONOFF_TOGGLE,
    ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE,
    ZCL_COVER_SWITCH_MODE_IMMEDIATE,
    ZCL_COVER_SWITCH_TYPE_MOMENTARY,
    ZCL_COVER_SWITCH_TYPE_TOGGLE,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE,
    ZCL_ONOFF_CONFIGURATION_RELAY_MODE_RISE,
    ZCL_WINDOW_COVERING_MOVING_CLOSING,
    ZCL_WINDOW_COVERING_MOVING_OPENING,
    ZCL_WINDOW_COVERING_MOVING_STOPPED,
)

# DEVICE_CONFIG LastError value for a config that does not parse.
DEVICE_CONFIG_ERROR_PARSE = 2
DEVICE_CONFIG_ERROR_NVM = 4
PAIR_HOLD_MS = 3000
SINGLE_HOLD_MS = 10_000
# Blinks are 250 ms on and 250 ms off; 3 blinks end within 1.5 s.
BLINKS_DONE_MS = 1600

# BSEED TS130F (vsj5o7dm): open B6, close A1, stop A0, relays D3/C0,
# direction LEDs C3/B5, backlight B4, red lock LED D2.
SHUTTER = "Mfr;Model;LB4;XB6A1A0u;CD3C0;IC3;IB5;PD2;M;"
SHUTTER_SWITCH_EP = 1
SHUTTER_COVER_EP = 2

# Two switch buttons A0/A1 driving relays A2/A3 (endpoints 3 and 4), lock LED C1.
TWO_GANG = "Mfr;Model;SA0u;RA2;SA1u;RA3;PC1;M;"
TWO_GANG_RELAY_EPS = (3, 4)

# Outlet: button B4, relay B5 (endpoint 2), relay indicator C4.
OUTLET = "Mfr;Model;SB4u;RB5;IC4;M;"
OUTLET_RELAY_EP = 2


def start(config: str) -> Iterator[Device]:
    p = StubProc(device_config=config).start()
    try:
        d = Device(p)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        yield d
    finally:
        p.stop()


@pytest.fixture
def shutter() -> Iterator[Device]:
    yield from start(SHUTTER)


@pytest.fixture
def two_gang() -> Iterator[Device]:
    yield from start(TWO_GANG)


@pytest.fixture
def outlet() -> Iterator[Device]:
    yield from start(OUTLET)


def lock_state(d: Device) -> str:
    return d.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK)


def last_error(d: Device) -> str:
    return d.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR)


def write_lock(d: Device, value: int) -> None:
    d.write_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK, value)


def led(d: Device, pin: str) -> int:
    return int(d.get_gpio(pin, refresh=True))


def led_switched_on(d: Device, pin: str) -> int:
    """Times the pin was driven high since the last clear_events()."""
    pin_id = d._parse_pin(pin)
    return sum(1 for evt in d._events
               if evt.kind == "gpio" and int(evt.payload.get("pin", "-1")) == pin_id
               and evt.payload.get("value") == "1")


def hold(d: Device, pins: tuple[str, ...], hold_ms: int) -> None:
    for pin in pins:
        d.press_button(pin)
    d.step_time(hold_ms)
    for pin in pins:
        d.release_button(pin)


def moving(d: Device) -> int:
    return d.zcl_cover_get_moving(SHUTTER_COVER_EP)


def bound_commands(d: Device) -> list:
    return d.zcl_list_cmds()


# ---------------------------------------------------------------------------
# Cover switch
# ---------------------------------------------------------------------------


def test_cover_hold_open_and_close_3s_locks_with_three_blinks(shutter: Device):
    assert lock_state(shutter) == "0"
    shutter.clear_events()

    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    shutter.step_time(BLINKS_DONE_MS)

    assert lock_state(shutter) == "1"
    assert led_switched_on(shutter, "D2") == 3
    assert led(shutter, "D2") == 0
    shutter.wait_for_attr_change(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK)


def test_cover_release_before_3s_does_not_lock(shutter: Device):
    shutter.clear_events()
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS - 200)
    shutter.step_time(BLINKS_DONE_MS)

    assert lock_state(shutter) == "0"
    assert led_switched_on(shutter, "D2") == 0


def test_cover_repress_restarts_the_3s_hold(shutter: Device):
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS - 500)
    shutter.press_button("B6")
    shutter.press_button("A1")
    shutter.step_time(1000)
    assert lock_state(shutter) == "0"
    shutter.step_time(PAIR_HOLD_MS - 1000)
    assert lock_state(shutter) == "1"


def test_cover_short_open_and_close_press_still_stops(shutter: Device):
    shutter.click_button("B6")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_OPENING

    hold(shutter, ("B6", "A1"), 300)
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)

    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert lock_state(shutter) == "0"


def test_cover_locked_buttons_do_nothing_and_blink_once(shutter: Device):
    write_lock(shutter, 1)
    for pin in ("B6", "A1", "A0"):
        shutter.clear_events()
        shutter.click_button(pin)
        shutter.step_time(MINIMUM_SWITCH_TIME_MS + 500)
        assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_STOPPED, pin
        assert led_switched_on(shutter, "D2") == 1, pin
        assert led(shutter, "D2") == 0, pin

    shutter.clear_events()
    shutter.long_click_button("A1", 1500)
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert (led(shutter, "D3"), led(shutter, "C0")) == (0, 0)
    assert bound_commands(shutter) == []


def test_cover_locked_press_does_not_act_in_immediate_mode(shutter: Device):
    shutter.zcl_cover_switch_set_local_mode(SHUTTER_SWITCH_EP, ZCL_COVER_SWITCH_MODE_IMMEDIATE)
    shutter.zcl_cover_switch_set_binded_mode(SHUTTER_SWITCH_EP, ZCL_COVER_SWITCH_MODE_IMMEDIATE)
    write_lock(shutter, 1)
    shutter.clear_events()

    shutter.press_button("B6")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert bound_commands(shutter) == []
    shutter.release_button("B6")


def test_cover_unlocked_buttons_send_bound_commands(shutter: Device):
    shutter.clear_events()
    shutter.click_button("B6")
    shutter.step_time(500)
    assert shutter.zcl_list_cmds(SHUTTER_SWITCH_EP, ZCL_CLUSTER_WINDOW_COVERING) != []


def test_cover_zigbee_commands_drive_a_locked_cover(shutter: Device):
    write_lock(shutter, 1)

    shutter.zcl_cover_open(SHUTTER_COVER_EP)
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_OPENING

    shutter.zcl_cover_stop(SHUTTER_COVER_EP)
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    shutter.zcl_cover_close(SHUTTER_COVER_EP)
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_CLOSING


def test_cover_hold_again_unlocks_with_two_blinks(shutter: Device):
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    shutter.step_time(BLINKS_DONE_MS)
    shutter.clear_events()

    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    shutter.step_time(BLINKS_DONE_MS)

    assert lock_state(shutter) == "0"
    # The two presses of the gesture start while locked and blink once each
    # (the second restarts the first blink); then the two unlock blinks.
    assert led_switched_on(shutter, "D2") == 2 + 2
    assert led(shutter, "D2") == 0

    shutter.click_button("B6")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_OPENING


def test_cover_lock_led_stays_dark_while_locked(shutter: Device):
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    shutter.step_time(10_000)
    assert lock_state(shutter) == "1"
    assert led(shutter, "D2") == 0


def test_remote_lock_write_is_silent_and_rejects_non_boolean(shutter: Device):
    shutter.clear_events()
    write_lock(shutter, 1)
    shutter.step_time(BLINKS_DONE_MS)
    assert lock_state(shutter) == "1"
    assert led_switched_on(shutter, "D2") == 0

    res = shutter.p.exec(f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
                         f"0x{ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK:04X} 2")
    assert not res.ok
    assert lock_state(shutter) == "1"

    write_lock(shutter, 0)
    shutter.click_button("B6")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_OPENING


def test_lock_survives_reboot(shutter: Device):
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    shutter.step_time(BLINKS_DONE_MS)
    shutter.p.stop()

    with StubProc() as p:
        d = Device(p)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        assert lock_state(d) == "1"
        assert led(d, "D2") == 0
        d.click_button("B6")
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        assert moving(d) == ZCL_WINDOW_COVERING_MOVING_STOPPED

        write_lock(d, 0)
    with StubProc() as p:
        assert lock_state(Device(p)) == "0"


def test_lock_survives_a_config_change(shutter: Device):
    write_lock(shutter, 1)
    shutter.write_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_TEXT,
                              SHUTTER.replace("Model", "Model2"))
    shutter.p.stop()
    with StubProc() as p:
        assert lock_state(Device(p)) == "1"


def test_cover_without_lock_led_falls_back_to_status_led():
    """Without `P` the status LED (backlight B4) carries the blinks and
    returns to its state afterwards."""
    yield_device = start("Mfr;Model;LB4;XB6A1A0u;CD3C0;IC3;IB5;M;")
    d = next(yield_device)
    try:
        backlight = led(d, "B4")
        d.clear_events()
        hold(d, ("B6", "A1"), PAIR_HOLD_MS)
        d.step_time(BLINKS_DONE_MS)
        assert lock_state(d) == "1"
        assert led_switched_on(d, "B4") >= 3
        assert led(d, "B4") == backlight
        assert (led(d, "C3"), led(d, "B5")) == (0, 0)
    finally:
        yield_device.close()


# ---------------------------------------------------------------------------
# Two switch buttons
# ---------------------------------------------------------------------------


def relays(d: Device) -> tuple[str, str]:
    return tuple(d.zcl_relay_get(ep) for ep in TWO_GANG_RELAY_EPS)


def test_two_buttons_short_press_still_toggles_both_relays(two_gang: Device):
    hold(two_gang, ("A0", "A1"), 100)
    two_gang.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relays(two_gang) == ("1", "1")
    assert lock_state(two_gang) == "0"


def test_two_buttons_held_3s_lock_without_toggling_relays(two_gang: Device):
    two_gang.clear_events()
    hold(two_gang, ("A0", "A1"), PAIR_HOLD_MS)
    two_gang.step_time(BLINKS_DONE_MS)

    assert lock_state(two_gang) == "1"
    assert relays(two_gang) == ("0", "0")
    assert led_switched_on(two_gang, "C1") == 3
    assert led(two_gang, "C1") == 0


def test_locked_switch_buttons_do_nothing(two_gang: Device):
    write_lock(two_gang, 1)
    two_gang.clear_events()
    for pin in ("A0", "A1"):
        two_gang.click_button(pin)
        two_gang.step_time(MINIMUM_SWITCH_TIME_MS)
    two_gang.long_click_button("A0", 1500)
    two_gang.step_time(MINIMUM_SWITCH_TIME_MS)

    assert relays(two_gang) == ("0", "0")
    assert bound_commands(two_gang) == []
    assert led_switched_on(two_gang, "C1") == 3


def test_locked_switch_press_does_not_act_in_rise_mode(two_gang: Device):
    two_gang.zcl_switch_relay_mode_set(1, ZCL_ONOFF_CONFIGURATION_RELAY_MODE_RISE)
    write_lock(two_gang, 1)

    two_gang.press_button("A0")
    two_gang.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relays(two_gang) == ("0", "0")
    two_gang.release_button("A0")


def test_locked_switch_relays_follow_zigbee(two_gang: Device):
    write_lock(two_gang, 1)
    two_gang.zcl_relay_on(TWO_GANG_RELAY_EPS[0])
    two_gang.call_zigbee_cmd(TWO_GANG_RELAY_EPS[1], ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE)
    two_gang.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relays(two_gang) == ("1", "1")


def test_two_buttons_hold_again_unlocks(two_gang: Device):
    write_lock(two_gang, 1)
    hold(two_gang, ("A0", "A1"), PAIR_HOLD_MS)
    two_gang.step_time(BLINKS_DONE_MS)
    assert lock_state(two_gang) == "0"
    # The gesture's own presses started locked and stay ignored.
    assert relays(two_gang) == ("0", "0")

    two_gang.click_button("A0")
    two_gang.step_time(MINIMUM_SWITCH_TIME_MS)
    assert relays(two_gang) == ("1", "0")


# ---------------------------------------------------------------------------
# Single-button outlet
# ---------------------------------------------------------------------------


def test_outlet_button_held_10s_locks(outlet: Device):
    hold(outlet, ("B4",), SINGLE_HOLD_MS - 500)
    outlet.step_time(BLINKS_DONE_MS)
    assert lock_state(outlet) == "0"

    hold(outlet, ("B4",), SINGLE_HOLD_MS)
    outlet.step_time(BLINKS_DONE_MS)
    assert lock_state(outlet) == "1"
    assert outlet.zcl_relay_get(OUTLET_RELAY_EP) == "0"


def test_outlet_locked_button_does_nothing_and_zigbee_still_works(outlet: Device):
    write_lock(outlet, 1)
    outlet.click_button("B4")
    outlet.step_time(MINIMUM_SWITCH_TIME_MS)
    assert outlet.zcl_relay_get(OUTLET_RELAY_EP) == "0"

    outlet.zcl_relay_on(OUTLET_RELAY_EP)
    outlet.step_time(MINIMUM_SWITCH_TIME_MS)
    assert outlet.zcl_relay_get(OUTLET_RELAY_EP) == "1"


def test_outlet_lock_blink_restores_a_lit_relay_indicator(outlet: Device):
    """No `P` token: the relay indicator C4 carries the blink."""
    outlet.zcl_relay_on(OUTLET_RELAY_EP)
    outlet.step_time(MINIMUM_SWITCH_TIME_MS)
    assert led(outlet, "C4") == 1
    write_lock(outlet, 1)

    outlet.press_button("B4")
    outlet.step_time(300)
    assert led(outlet, "C4") == 0
    outlet.release_button("B4")
    outlet.step_time(BLINKS_DONE_MS)
    assert led(outlet, "C4") == 1
    assert outlet.zcl_relay_get(OUTLET_RELAY_EP) == "1"


def test_lock_led_token_on_an_indicator_pin_shares_it():
    gen = start("Mfr;Model;SB4u;RB5;IC4;PC4;M;")
    d = next(gen)
    try:
        d.zcl_relay_on(OUTLET_RELAY_EP)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        d.clear_events()
        hold(d, ("B4",), SINGLE_HOLD_MS)
        d.step_time(300)
        assert led(d, "C4") == 0
        d.step_time(BLINKS_DONE_MS)
        assert lock_state(d) == "1"
        assert led(d, "C4") == 1
        assert led_switched_on(d, "C4") >= 3

        d.zcl_relay_off(OUTLET_RELAY_EP)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        assert led(d, "C4") == 0
    finally:
        gen.close()


@pytest.mark.parametrize("token", ["PZ9", "PC4x", "PC4i;PC3"])
def test_bad_lock_led_tokens_boot_the_recovery_graph(token: str):
    with StubProc(device_config=f"Mfr;Model;SB4u;RB5;{token};") as p:
        assert Device(p).read_zigbee_attr(
            1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR
        ) == str(DEVICE_CONFIG_ERROR_PARSE)


def test_inverted_lock_led_is_driven_low_to_blink():
    gen = start("Mfr;Model;SB4u;RB5;PC3i;M;")
    d = next(gen)
    try:
        assert led(d, "C3") == 1
        write_lock(d, 1)
        d.press_button("B4")
        d.step_time(100)
        assert led(d, "C3") == 0
        d.release_button("B4")
        d.step_time(BLINKS_DONE_MS)
        assert led(d, "C3") == 1
    finally:
        gen.close()


# ---------------------------------------------------------------------------
# Toggle (latching) inputs never lock
# ---------------------------------------------------------------------------


def test_toggle_outlet_switch_held_on_does_not_lock(outlet: Device):
    """A rocker in toggle mode stays closed while the load is on."""
    outlet.zcl_switch_mode_set(1, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE)
    outlet.press_button("B4")
    outlet.step_time(SINGLE_HOLD_MS + 2000)
    assert lock_state(outlet) == "0"
    assert outlet.zcl_relay_get(OUTLET_RELAY_EP) == "1"
    outlet.release_button("B4")

    outlet.zcl_switch_mode_set(1, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY)
    hold(outlet, ("B4",), SINGLE_HOLD_MS)
    assert lock_state(outlet) == "1"


def test_toggle_two_gang_both_on_does_not_lock(two_gang: Device):
    two_gang.zcl_switch_mode_set(1, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE)
    hold(two_gang, ("A0", "A1"), PAIR_HOLD_MS + 1000)
    assert lock_state(two_gang) == "0"

    two_gang.zcl_switch_mode_set(1, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY)
    hold(two_gang, ("A0", "A1"), PAIR_HOLD_MS)
    assert lock_state(two_gang) == "1"


def test_switch_type_changed_to_toggle_during_the_hold_does_not_lock(two_gang: Device):
    two_gang.press_button("A0")
    two_gang.press_button("A1")
    two_gang.step_time(1000)
    two_gang.zcl_switch_mode_set(2, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE)
    two_gang.step_time(PAIR_HOLD_MS)
    assert lock_state(two_gang) == "0"


def test_toggle_cover_switch_does_not_lock(shutter: Device):
    shutter.zcl_cover_switch_set_switch_type(SHUTTER_SWITCH_EP, ZCL_COVER_SWITCH_TYPE_TOGGLE)
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS + 1000)
    assert lock_state(shutter) == "0"

    shutter.zcl_cover_switch_set_switch_type(SHUTTER_SWITCH_EP, ZCL_COVER_SWITCH_TYPE_MOMENTARY)
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    assert lock_state(shutter) == "1"


# ---------------------------------------------------------------------------
# Factory reset and storage failures
# ---------------------------------------------------------------------------


def test_multi_press_factory_reset_releases_the_lock(outlet: Device):
    write_lock(outlet, 1)
    outlet.click_n("B4", 10)
    outlet.step_time(1000)

    assert lock_state(outlet) == "0"
    assert not outlet.is_joined()
    outlet.p.stop()
    with StubProc() as p:
        assert lock_state(Device(p)) == "0"


def test_failed_store_keeps_the_gesture_from_toggling(shutter: Device):
    shutter.clear_events()
    assert shutter.p.exec("nvm_fail 1").ok
    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    shutter.step_time(BLINKS_DONE_MS)

    assert lock_state(shutter) == "0"
    assert led_switched_on(shutter, "D2") == 0
    assert last_error(shutter) == str(DEVICE_CONFIG_ERROR_NVM)
    shutter.click_button("B6")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_OPENING
    shutter.p.stop()
    with StubProc() as p:
        assert lock_state(Device(p)) == "0"


def test_failed_remote_write_keeps_the_lock(shutter: Device):
    assert shutter.p.exec("nvm_fail 1").ok
    res = shutter.p.exec(f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
                         f"0x{ZCL_ATTR_DEVICE_CONFIG_CHILD_LOCK:04X} 1")
    assert not res.ok
    assert lock_state(shutter) == "0"
    shutter.click_button("B6")
    shutter.step_time(MINIMUM_SWITCH_TIME_MS)
    assert moving(shutter) == ZCL_WINDOW_COVERING_MOVING_OPENING


def test_gesture_keeps_an_unrelated_last_error(shutter: Device):
    assert shutter.p.exec("nvm_fail 1").ok
    res = shutter.p.exec(f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
                         f"0x{ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE:04X} 1")
    assert not res.ok
    assert last_error(shutter) == str(DEVICE_CONFIG_ERROR_NVM)

    hold(shutter, ("B6", "A1"), PAIR_HOLD_MS)
    assert lock_state(shutter) == "1"
    assert last_error(shutter) == str(DEVICE_CONFIG_ERROR_NVM)
