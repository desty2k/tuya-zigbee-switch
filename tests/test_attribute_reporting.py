"""State attributes the integrations configure reporting on must be reportable.

Telink and Silabs report only attributes flagged HAL_ATTR_REPORT and answer
Configure Reporting for any other attribute with UNREPORTABLE_ATTRIBUTE; the
stub does the same.
"""

import pytest

from client import StubProc
from conftest import Device, RelayButtonPair
from zcl_consts import (
    ZCL_ATTR_BASIC_MODEL_ID,
    ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE,
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_ACTIONS,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_ATTR_ONOFF_INDICATOR_STATE,
    ZCL_ATTR_POWER_CFG_BATTERY_PERCENTAGE,
    ZCL_ATTR_POWER_CFG_BATTERY_VOLTAGE,
    ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE,
    ZCL_ATTR_WINDOW_COVERING_MOVING,
    ZCL_CLUSTER_BASIC,
    ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
    ZCL_CLUSTER_POWER_CFG,
    ZCL_CLUSTER_WINDOW_COVERING,
    ZCL_CMD_WINDOW_COVERING_UP_OPEN,
)

# Switch EP1, relay with indicator EP2, cover switch EP3, cover EP4, battery.
REPORTING_CONFIG = "M;Report;SA2u;RB2;IC5;XA0A1u;CB0B1;BTA5;"


def _configure_reporting(proc: StubProc, ep: int, cluster: int, attr: int):
    return proc.exec(f"zcl_configure_reporting {ep} 0x{cluster:04X} 0x{attr:04X}")


@pytest.mark.parametrize(
    "ep, cluster, attr",
    [
        (2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF),
        (2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_STATE),
        (1, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC, ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE),
        (1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_ACTIONS),
        (3, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC, ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE),
        (4, ZCL_CLUSTER_WINDOW_COVERING,
         ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE),
        (4, ZCL_CLUSTER_WINDOW_COVERING, ZCL_ATTR_WINDOW_COVERING_MOVING),
        (1, ZCL_CLUSTER_POWER_CFG, ZCL_ATTR_POWER_CFG_BATTERY_VOLTAGE),
        (1, ZCL_CLUSTER_POWER_CFG, ZCL_ATTR_POWER_CFG_BATTERY_PERCENTAGE),
    ],
)
def test_integration_reported_attributes_accept_configure_reporting(
    ep: int, cluster: int, attr: int
) -> None:
    proc = StubProc(device_config=REPORTING_CONFIG).start()
    try:
        res = _configure_reporting(proc, ep, cluster, attr)
        assert res.ok, res.payload
    finally:
        proc.stop()


@pytest.mark.parametrize(
    "ep, cluster, attr",
    [
        (1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MODEL_ID),
        (1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE),
    ],
)
def test_configuration_attributes_reject_configure_reporting(
    ep: int, cluster: int, attr: int
) -> None:
    proc = StubProc(device_config=REPORTING_CONFIG).start()
    try:
        res = _configure_reporting(proc, ep, cluster, attr)
        assert not res.ok
        assert "unreportable_attribute" in res.payload
    finally:
        proc.stop()


def test_local_press_reports_relay_on_off(
    device: Device, relay_button_pair: RelayButtonPair
) -> None:
    device.clear_events()
    device.click_button(relay_button_pair.button_pin)
    device.wait_for_attr_change(relay_button_pair.relay_endpoint, ZCL_CLUSTER_ON_OFF,
                                ZCL_ATTR_ONOFF)


def test_cover_move_reports_moving_and_position() -> None:
    proc = StubProc(device_config="M;Cover;CB0B1;").start()
    try:
        device = Device(proc)
        device.call_zigbee_cmd(1, ZCL_CLUSTER_WINDOW_COVERING,
                               ZCL_CMD_WINDOW_COVERING_UP_OPEN)
        device.wait_for_attr_change(1, ZCL_CLUSTER_WINDOW_COVERING,
                                    ZCL_ATTR_WINDOW_COVERING_MOVING)
    finally:
        proc.stop()
