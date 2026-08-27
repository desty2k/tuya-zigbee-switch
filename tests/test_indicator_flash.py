"""Tests for indicator LED flash on button press/release."""

from typing import Iterator

import pytest

from tests.client import StubProc
from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_INDICATOR_MODE,
    ZCL_CLUSTER_ON_OFF,
    ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED,
    ZCL_ONOFF_INDICATOR_MODE_SAME,
)


@pytest.fixture()
def indicator_device() -> Iterator[Device]:
    # One switch, one relay, indicator LED on A1
    cfg = "X;Y;SA0u;RB0;IA1;"
    with StubProc(device_config=cfg) as proc:
        yield Device(proc)


def test_no_flash_when_relay_attached(indicator_device: Device) -> None:
    """Indicator must follow relay state without spurious blink when relay is attached."""
    relay_ep = 2

    indicator_device.write_zigbee_attr(
        relay_ep, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_SAME,
    )
    # Turn relay ON via button press (toggle mode: press=ON, release=OFF)
    indicator_device.press_button("A0")
    # Indicator should be ON (following relay)
    assert indicator_device.get_gpio("A1", refresh=True)

    # Wait long enough for a blink cycle to finish (if one were started)
    indicator_device.step_time(200)
    # Indicator must still follow relay (ON), not be turned off by a stale blink
    assert indicator_device.get_gpio("A1", refresh=True)
    assert indicator_device.read_zigbee_attr(relay_ep, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF) == "1"

    indicator_device.release_button("A0")


def test_flash_in_detached_mode(indicator_device: Device) -> None:
    """Indicator should briefly flash on button press when relay is detached."""
    relay_ep = 2

    indicator_device.write_zigbee_attr(
        relay_ep, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_SAME,
    )
    # Detach relay from button
    indicator_device.zcl_switch_relay_mode_set(1, ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED)

    # Relay is OFF, indicator is OFF
    assert not indicator_device.get_gpio("A1", refresh=True)

    # Press button — should start a flash (LED momentarily ON)
    indicator_device.press_button("A0")
    assert indicator_device.get_gpio("A1", refresh=True)

    # After blink finishes, LED should be back OFF
    indicator_device.step_time(200)
    assert not indicator_device.get_gpio("A1", refresh=True)

    indicator_device.release_button("A0")


def _detach_switch(device: Device) -> None:
    device.write_zigbee_attr(
        2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_SAME,
    )
    device.zcl_switch_relay_mode_set(1, ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED)


def test_single_click_has_no_delayed_confirmation(indicator_device: Device) -> None:
    """A normal detached press keeps its immediate pulse without a delayed echo."""
    _detach_switch(indicator_device)

    indicator_device.click_button("A0")
    indicator_device.step_time(100)
    assert not indicator_device.get_gpio("A1", refresh=True)

    indicator_device.step_time(249)
    assert not indicator_device.get_gpio("A1", refresh=True)
    indicator_device.step_time(1)
    assert not indicator_device.get_gpio("A1", refresh=True)


@pytest.mark.parametrize("count", [2, 3], ids=["double", "triple"])
def test_finalized_click_count_has_compact_confirmation(
    indicator_device: Device, count: int
) -> None:
    """Detached double and triple clicks are confirmed only after the click gap."""
    _detach_switch(indicator_device)

    indicator_device.click_n("A0", count)
    indicator_device.step_time(100)
    assert not indicator_device.get_gpio("A1", refresh=True)

    indicator_device.step_time(249)
    assert not indicator_device.get_gpio("A1", refresh=True)
    indicator_device.step_time(1)
    assert indicator_device.get_gpio("A1", refresh=True)

    for pulse in range(count):
        indicator_device.step_time(60)
        assert not indicator_device.get_gpio("A1", refresh=True)
        if pulse + 1 < count:
            indicator_device.step_time(70)
            assert indicator_device.get_gpio("A1", refresh=True)

    indicator_device.step_time(70)
    assert not indicator_device.get_gpio("A1", refresh=True)


def test_network_disconnected_blink_has_priority(indicator_device: Device) -> None:
    """Detached button feedback must not restart the disconnected-network blink."""
    _detach_switch(indicator_device)
    indicator_device.set_network(0)
    assert indicator_device.get_gpio("A1", refresh=True)

    indicator_device.click_n("A0", 2)
    indicator_device.step_time(349)
    assert indicator_device.get_gpio("A1", refresh=True)

    indicator_device.step_time(1)
    assert indicator_device.get_gpio("A1", refresh=True)
    indicator_device.step_time(10)
    assert not indicator_device.get_gpio("A1", refresh=True)
    indicator_device.step_time(500)
    assert indicator_device.get_gpio("A1", refresh=True)
