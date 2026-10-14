"""Tests for cover functionality (input and output clusters)."""
import pytest

from client import StubProc
from conftest import Device, MINIMUM_SWITCH_TIME_MS
from zcl_consts import (
    ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME,
    ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE,
    ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE,
    ZCL_ATTR_WINDOW_COVERING_OPEN_TIME,
    ZCL_ATTR_WINDOW_COVERING_OVERRUN,
    ZCL_CLUSTER_WINDOW_COVERING,
    ZCL_CMD_WINDOW_COVERING_GOTO_LIFT_PERCENTAGE,
    ZCL_WINDOW_COVERING_MOVING_STOPPED,
    ZCL_WINDOW_COVERING_MOVING_OPENING,
    ZCL_WINDOW_COVERING_MOVING_CLOSING,
)


@pytest.fixture
def cover_device() -> Device:
    p = StubProc(device_config="X;Y;CA0A1;").start()
    try:
        d = Device(p)
        # Advance time past the motor protection delay. Rather than adding extra logic to the firmware code
        # to allow immediate command execution, we handle this constraint in the test setup by advancing time here.
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        yield d
    finally:
        p.stop()


def test_cover_open(cover_device: Device):
    d = cover_device
    endpoint = 1

    # Act
    d.zcl_cover_open(endpoint)

    # Assert
    assert d.get_gpio("A0", refresh=True)
    assert not d.get_gpio("A1", refresh=True)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_OPENING


def test_cover_close(cover_device: Device):
    d = cover_device
    endpoint = 1

    # Act
    d.zcl_cover_close(endpoint)

    # Assert
    assert not d.get_gpio("A0", refresh=True)
    assert d.get_gpio("A1", refresh=True)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_CLOSING


def test_cover_open_reversed(cover_device: Device):
    d = cover_device
    endpoint = 1

    # Arrange
    d.zcl_cover_motor_reversal_set(endpoint, 1)

    # Act
    d.zcl_cover_open(endpoint)

    # Assert
    assert not d.get_gpio("A0", refresh=True)
    assert d.get_gpio("A1", refresh=True)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_OPENING


def test_cover_close_reversed(cover_device: Device):
    d = cover_device
    endpoint = 1

    # Arrange
    d.zcl_cover_motor_reversal_set(endpoint, 1)

    # Act
    d.zcl_cover_close(endpoint)

    # Assert
    assert d.get_gpio("A0", refresh=True)
    assert not d.get_gpio("A1", refresh=True)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_CLOSING


def test_cover_stop(cover_device: Device):
    d = cover_device
    endpoint = 1

    # Arrange: Start the cover and wait for the minimum switch time
    d.zcl_cover_open(endpoint)
    d.step_time(MINIMUM_SWITCH_TIME_MS)

    # Act
    d.zcl_cover_stop(endpoint)

    # Assert
    assert not d.get_gpio("A0", refresh=True)
    assert not d.get_gpio("A1", refresh=True)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_STOPPED


def test_cover_immediate_restart(cover_device: Device):
    d = cover_device
    endpoint = 1

    # Arrange: Start and stop the cover
    d.zcl_cover_open(endpoint)
    d.step_time(MINIMUM_SWITCH_TIME_MS)
    d.zcl_cover_stop(endpoint)

    # Act: Restart the cover immediately
    d.zcl_cover_open(endpoint)

    # Assert: The cover should restart only after the minimum switch time
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    d.step_time(MINIMUM_SWITCH_TIME_MS)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_OPENING


def test_cover_immediate_reversal(cover_device: Device):
    d = cover_device
    endpoint = 1

    # Arrange: Start the cover moving
    d.zcl_cover_open(endpoint)

    # Act: Reverse the direction immediately
    d.zcl_cover_close(endpoint)

    # The applied relay transition stops motion before the interlock gap.
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    d.step_time(MINIMUM_SWITCH_TIME_MS)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_CLOSING


def test_cover_goto_reports_calibrated_position_and_endpoint_overrun(cover_device: Device):
    d = cover_device
    endpoint = 1

    d.write_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                        ZCL_ATTR_WINDOW_COVERING_OPEN_TIME, 1000)
    d.write_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                        ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME, 1000)
    d.write_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                        ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE, 1000)
    d.write_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                        ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE, 1000)
    d.write_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                        ZCL_ATTR_WINDOW_COVERING_OVERRUN, 100)

    d.zcl_cover_goto(endpoint, 75)
    d.step_time(700)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert d.zcl_cover_get_position(endpoint) == 75

    d.step_time(MINIMUM_SWITCH_TIME_MS)
    d.zcl_cover_open(endpoint)
    d.step_time(700)
    assert d.zcl_cover_get_position(endpoint) == 0
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_OPENING
    d.step_time(100)
    assert d.zcl_cover_get_moving(endpoint) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert d.zcl_cover_get_position(endpoint) == 0


def test_cover_rejects_manual_goto_and_invalid_deadzone_write(cover_device: Device):
    d = cover_device
    endpoint = 1

    result = d.call_zigbee_cmd_raw(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                                    ZCL_CMD_WINDOW_COVERING_GOTO_LIFT_PERCENTAGE,
                                    bytes([50]))
    assert result["result"] == "ACTION_DENIED"
    result = d.call_zigbee_cmd_raw(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                                    ZCL_CMD_WINDOW_COVERING_GOTO_LIFT_PERCENTAGE)
    assert result["result"] == "MALFORMED_COMMAND"

    d.write_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                        ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE, 9999)
    d.write_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                        ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE, 1)
    assert int(d.read_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                                  ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE)) == 0
