from typing import Iterator

import pytest

from tests.client import StubProc
from tests.conftest import Device, RelayButtonPair
from tests.test_network_join import (
    HAL_ZIGBEE_NETWORK_JOINED,
    HAL_ZIGBEE_NETWORK_NOT_JOINED,
)
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_INDICATOR_MODE,
    ZCL_ATTR_ONOFF_INDICATOR_STATE,
    ZCL_ATTR_START_UP_ONOFF,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CMD_ONOFF_OFF,
    ZCL_CMD_ONOFF_ON,
    ZCL_CMD_ONOFF_TOGGLE,
    ZCL_ONOFF_INDICATOR_MODE_MANUAL,
    ZCL_ONOFF_INDICATOR_MODE_OPPOSITE,
    ZCL_ONOFF_INDICATOR_MODE_SAME,
    ZCL_START_UP_ONOFF_SET_ONOFF_TO_OFF,
    ZCL_START_UP_ONOFF_SET_ONOFF_TO_ON,
    ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS,
    ZCL_START_UP_ONOFF_SET_ONOFF_TOGGLE,
)


@pytest.mark.parametrize(
    "cmd_id,expected",
    [
        (ZCL_CMD_ONOFF_ON, True),
        (ZCL_CMD_ONOFF_OFF, False),
    ],
)
def test_relay_onoff_toggle_commands(
    device: Device,
    relay_button_pairs: list[RelayButtonPair],
    cmd_id: int,
    expected: bool,
):
    for pair in relay_button_pairs:
        device.call_zigbee_cmd(pair.relay_endpoint, ZCL_CLUSTER_ON_OFF, cmd_id)
        assert device.read_zigbee_attr(
            pair.relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF
        ) == ("1" if expected else "0")
        assert device.get_gpio(pair.relay_pin) == expected, pair.relay_pin


def test_relay_toggle_commands(
    device: Device,
    relay_button_pairs: list[RelayButtonPair],
):
    for pair in relay_button_pairs:
        device.zcl_relay_off(pair.relay_endpoint)
        device.call_zigbee_cmd(
            pair.relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE
        )
        assert (
            device.read_zigbee_attr(
                pair.relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF
            )
            == "1"
        )
        assert device.get_gpio(pair.relay_pin)

        device.call_zigbee_cmd(
            pair.relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE
        )
        assert (
            device.read_zigbee_attr(
                pair.relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF
            )
            == "0"
        )
        assert not device.get_gpio(pair.relay_pin)


@pytest.fixture()
def indicator_device() -> Iterator[Device]:
    cfg = "X;Y;SA0u;RB0;IA1;"  # One switch, one relay, indicator LED on pin A1
    with StubProc(device_config=cfg) as proc:
        yield Device(proc)


def test_indicator_mode_same(indicator_device: Device) -> None:
    relay_endpoint = 2

    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_SAME,
    )
    indicator_device.zcl_relay_on(relay_endpoint)
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "1"
    )
    assert indicator_device.get_gpio("A1")
    indicator_device.zcl_relay_off(relay_endpoint)
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "0"
    )
    assert not indicator_device.get_gpio("A1")


def test_indicator_mode_opposite(indicator_device: Device) -> None:
    relay_endpoint = 2

    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_OPPOSITE,
    )
    indicator_device.zcl_relay_on(relay_endpoint)
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "0"
    )
    assert not indicator_device.get_gpio("A1")
    indicator_device.zcl_relay_off(relay_endpoint)
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "1"
    )
    assert indicator_device.get_gpio("A1")


def test_indicator_mode_manual_does_not_change_state(indicator_device: Device) -> None:
    relay_endpoint = 2

    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_MANUAL,
    )
    indicator_device.zcl_relay_on(relay_endpoint)
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "0"
    )
    assert not indicator_device.get_gpio("A1")
    indicator_device.zcl_relay_off(relay_endpoint)
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "0"
    )
    assert not indicator_device.get_gpio("A1")


def test_indicator_mode_manual_controlled(indicator_device: Device) -> None:
    relay_endpoint = 2

    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_MANUAL,
    )
    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_STATE,
        "1",
    )
    assert indicator_device.get_gpio("A1")
    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_STATE,
        "0",
    )
    assert not indicator_device.get_gpio("A1")


def _toggle_network(indicator_device: Device) -> None:
    indicator_device.set_network(HAL_ZIGBEE_NETWORK_NOT_JOINED)
    indicator_device.step_time(500)
    indicator_device.set_network(HAL_ZIGBEE_NETWORK_JOINED)
    indicator_device.step_time(500)


def test_indicator_mode_restored_after_network_loss(indicator_device: Device) -> None:
    relay_endpoint = 2

    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_SAME,
    )
    indicator_device.zcl_relay_on(relay_endpoint)

    _toggle_network(indicator_device)

    assert indicator_device.get_gpio("A1")
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "1"
    )

    indicator_device.zcl_relay_off(relay_endpoint)

    _toggle_network(indicator_device)

    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "0"
    )
    assert not indicator_device.get_gpio("A1")


def test_indicator_mode_restored_after_network_loss_manual(
    indicator_device: Device,
) -> None:
    relay_endpoint = 2

    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_MODE,
        ZCL_ONOFF_INDICATOR_MODE_MANUAL,
    )
    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_STATE,
        "1",
    )

    _toggle_network(indicator_device)

    assert indicator_device.get_gpio("A1")
    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "1"
    )

    indicator_device.write_zigbee_attr(
        relay_endpoint,
        ZCL_CLUSTER_ON_OFF,
        ZCL_ATTR_ONOFF_INDICATOR_STATE,
        "0",
    )

    _toggle_network(indicator_device)

    assert (
        indicator_device.read_zigbee_attr(
            relay_endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
        )
        == "0"
    )
    assert not indicator_device.get_gpio("A1")


@pytest.mark.parametrize(
    "indicator_state",
    [
        "1",
        "0",
    ],
)
def test_manual_indicator_preserver_after_reboot(indicator_state: bool) -> None:
    device_config = "A;B;RB0;IB1;"
    endpoint = 1

    # First session: set mode and relay state
    with StubProc(device_config=device_config) as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            endpoint,
            ZCL_CLUSTER_ON_OFF,
            ZCL_ATTR_ONOFF_INDICATOR_MODE,
            ZCL_ONOFF_INDICATOR_MODE_MANUAL,
        )
        device.write_zigbee_attr(
            endpoint,
            ZCL_CLUSTER_ON_OFF,
            ZCL_ATTR_ONOFF_INDICATOR_STATE,
            indicator_state,
        )

    # Second session: restart device and check indicator state
    with StubProc(device_config=device_config) as proc:
        device = Device(proc)

        assert device.get_gpio("B1") == (indicator_state == "1")
        assert (
            device.read_zigbee_attr(
                endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE
            )
            == indicator_state
        )


@pytest.mark.parametrize(
    "mode,before_state,after_state",
    [
        (ZCL_START_UP_ONOFF_SET_ONOFF_TO_OFF, False, False),
        (ZCL_START_UP_ONOFF_SET_ONOFF_TO_OFF, True, False),
        (ZCL_START_UP_ONOFF_SET_ONOFF_TO_ON, False, True),
        (ZCL_START_UP_ONOFF_SET_ONOFF_TO_ON, True, True),
        (ZCL_START_UP_ONOFF_SET_ONOFF_TOGGLE, False, True),
        (ZCL_START_UP_ONOFF_SET_ONOFF_TOGGLE, True, False),
        (ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS, False, False),
        (ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS, True, True),
    ],
)
def test_relay_cluster_startup_attr(
    mode: int, before_state: bool, after_state: bool
) -> None:
    """Test that writable switch cluster attributes are preserved across device restarts via NVM"""
    device_config = "A;B;RB0;"  # Two switches and two relays
    endpoint = 1

    # First session: set mode and relay state
    with StubProc(device_config=device_config) as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_START_UP_ONOFF, mode
        )
        if before_state:
            device.zcl_relay_on(endpoint)
        else:
            device.zcl_relay_off(endpoint)

    # Second session: restart device and check relay state
    with StubProc(device_config=device_config) as proc:
        device = Device(proc)

        assert device.zcl_relay_get(endpoint) == ("1" if after_state else "0")
        assert device.get_gpio("B0") == after_state


OUTLET_CONFIG = "X;Y;SB4u;RB5;IC4;M;"  # TS011F outlet: relay on endpoint 2, LED C4


def _indicator(device: Device) -> str:
    return device.read_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE)


@pytest.mark.parametrize(
    "mode, expected",
    [(ZCL_ONOFF_INDICATOR_MODE_SAME, "0"), (ZCL_ONOFF_INDICATOR_MODE_OPPOSITE, "1")],
)
def test_indicator_state_write_outside_manual_is_overridden_and_reported(
    mode: int, expected: str
) -> None:
    with StubProc(device_config=OUTLET_CONFIG) as proc:
        device = Device(proc)
        device.write_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, mode)
        device.clear_events()
        written = "0" if expected == "1" else "1"
        device.write_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE, written)
        device.wait_for_attr_change(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE)
        assert _indicator(device) == expected
        assert device.get_gpio("C4", refresh=True) is (expected == "1")


def test_entering_manual_mode_shows_the_stored_indicator_state() -> None:
    """Hardware sequence: same mode, relay off, write state 1, then manual."""
    with StubProc(device_config=OUTLET_CONFIG) as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_ONOFF_INDICATOR_MODE_SAME
        )
        device.zcl_relay_off(2)
        device.write_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE, "1")
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_ONOFF_INDICATOR_MODE_MANUAL
        )
        assert device.read_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF) == "0"
        assert device.read_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE
        ) == str(ZCL_ONOFF_INDICATOR_MODE_MANUAL)
        assert _indicator(device) == "0"
        assert not device.get_gpio("C4", refresh=True)


def test_entering_manual_mode_applies_the_state_written_in_manual() -> None:
    with StubProc(device_config=OUTLET_CONFIG) as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_ONOFF_INDICATOR_MODE_MANUAL
        )
        device.write_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE, "1")
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_ONOFF_INDICATOR_MODE_SAME
        )
        assert _indicator(device) == "0"
        assert not device.get_gpio("C4", refresh=True)
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_ONOFF_INDICATOR_MODE_MANUAL
        )
        device.write_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE, "1")
        assert _indicator(device) == "1"
        assert device.get_gpio("C4", refresh=True)


def test_undivided_manual_mode_and_state_write_lights_the_led() -> None:
    with StubProc(device_config=OUTLET_CONFIG) as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_MODE, ZCL_ONOFF_INDICATOR_MODE_SAME
        )
        res = proc.exec(
            f"zcl_write_undivided 2 0x{ZCL_CLUSTER_ON_OFF:04X} "
            f"0x{ZCL_ATTR_ONOFF_INDICATOR_MODE:04X}={ZCL_ONOFF_INDICATOR_MODE_MANUAL:02x} "
            f"0x{ZCL_ATTR_ONOFF_INDICATOR_STATE:04X}=01"
        )
        assert res.ok, res.payload
        assert _indicator(device) == "1"
        assert device.get_gpio("C4", refresh=True)
