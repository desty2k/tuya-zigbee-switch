"""Tests for cover functionality (input and output clusters)."""
import pytest

from client import StubProc
from conftest import Device, MINIMUM_SWITCH_TIME_MS
from zcl_consts import (
    ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME,
    ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE,
    ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE,
    ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE,
    ZCL_ATTR_WINDOW_COVERING_OPEN_TIME,
    ZCL_ATTR_WINDOW_COVERING_OVERRUN,
    ZCL_CLUSTER_WINDOW_COVERING,
    ZCL_CMD_WINDOW_COVERING_GOTO_LIFT_PERCENTAGE,
    ZCL_CMD_WINDOW_COVERING_UP_OPEN,
    ZCL_WINDOW_COVERING_MOVING_STOPPED,
    ZCL_WINDOW_COVERING_MOVING_OPENING,
    ZCL_WINDOW_COVERING_MOVING_CLOSING,
)

COVER_CONFIG = "X;Y;CA0A1;"
COVER_EP = 1
TRAVEL_MS = 1000
OVERRUN_MS = 100
# ZCL marks an unknown uint8 value with 0xFF; Z2M ignores lift values > 100.
UNKNOWN_POSITION = 0xFF


def calibrate(d: Device, travel_ms: int = TRAVEL_MS, overrun_ms: int = OVERRUN_MS) -> None:
    for attr, value in ((ZCL_ATTR_WINDOW_COVERING_OPEN_TIME, travel_ms),
                        (ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME, travel_ms),
                        (ZCL_ATTR_WINDOW_COVERING_OVERRUN, overrun_ms)):
        d.write_zigbee_attr(COVER_EP, ZCL_CLUSTER_WINDOW_COVERING, attr, value)


def goto_result(d: Device, lift_percentage: int) -> str:
    return d.call_zigbee_cmd_raw(COVER_EP, ZCL_CLUSTER_WINDOW_COVERING,
                                 ZCL_CMD_WINDOW_COVERING_GOTO_LIFT_PERCENTAGE,
                                 bytes([lift_percentage]))["result"]


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
    d.zcl_cover_close(endpoint)
    d.step_time(1100)
    d.step_time(MINIMUM_SWITCH_TIME_MS)
    assert d.zcl_cover_get_position(endpoint) == 100

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
    # The controller refuses deadzones that overlap; the write fails and the
    # attribute keeps its value.
    res = d.p.exec(f"zcl_write {endpoint} 0x{ZCL_CLUSTER_WINDOW_COVERING:04X} "
                   f"0x{ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE:04X} 1")
    assert not res.ok
    assert int(d.read_zigbee_attr(endpoint, ZCL_CLUSTER_WINDOW_COVERING,
                                  ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE)) == 0


def test_wire_position_uses_zcl_lift_percentage(cover_device: Device):
    """H7: CurrentPositionLiftPercentage is % closed (0 = open, 100 = closed)."""
    d = cover_device
    calibrate(d)
    assert d.zcl_cover_get_position(COVER_EP) == UNKNOWN_POSITION

    d.zcl_cover_close(COVER_EP)
    d.step_time(TRAVEL_MS + OVERRUN_MS)
    assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert d.zcl_cover_get_position(COVER_EP) == 100

    d.step_time(MINIMUM_SWITCH_TIME_MS)
    d.zcl_cover_open(COVER_EP)
    d.step_time(TRAVEL_MS + OVERRUN_MS)
    assert d.zcl_cover_get_position(COVER_EP) == 0

    # 30 % open is lift percentage 70.
    d.step_time(MINIMUM_SWITCH_TIME_MS)
    assert goto_result(d, 70) == "PROCESSED"
    assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_CLOSING
    d.step_time(TRAVEL_MS)
    assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert d.zcl_cover_get_position(COVER_EP) == 70


def test_goto_is_refused_while_position_is_unknown(cover_device: Device):
    d = cover_device
    calibrate(d)

    assert goto_result(d, 50) == "ACTION_DENIED"
    assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert not d.get_gpio("A0", refresh=True)
    assert not d.get_gpio("A1", refresh=True)


def test_open_without_a_cutoff_timer_is_denied_and_leaves_the_motor_off(cover_device: Device):
    d = cover_device
    # Both attempts to arm the cutoff timer fail.
    d.task_fail(2)

    result = d.call_zigbee_cmd_raw(COVER_EP, ZCL_CLUSTER_WINDOW_COVERING,
                                   ZCL_CMD_WINDOW_COVERING_UP_OPEN)

    assert result["result"] == "ACTION_DENIED"
    assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED
    assert not d.get_gpio("A0", refresh=True)
    assert not d.get_gpio("A1", refresh=True)


def test_power_loss_mid_move_runs_full_travel_before_position_is_known():
    """H6: the stored position is stale after power loss during motion."""
    with StubProc(device_config=COVER_CONFIG) as p:
        d = Device(p)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        calibrate(d)
        d.zcl_cover_close(COVER_EP)
        d.step_time(TRAVEL_MS + OVERRUN_MS)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        assert goto_result(d, 50) == "PROCESSED"
        d.step_time(TRAVEL_MS)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        assert d.zcl_cover_get_position(COVER_EP) == 50
        # Power is lost a quarter of the travel into the close: the stored
        # position stays at 50 while the cover is at about 75.
        d.zcl_cover_close(COVER_EP)
        d.step_time(TRAVEL_MS // 4)
        assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_CLOSING

    with StubProc() as p:
        d = Device(p)
        d.step_time(MINIMUM_SWITCH_TIME_MS)
        assert d.zcl_cover_get_position(COVER_EP) == UNKNOWN_POSITION
        assert goto_result(d, 50) == "ACTION_DENIED"

        d.zcl_cover_open(COVER_EP)
        d.step_time(TRAVEL_MS)
        assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_OPENING
        assert d.zcl_cover_get_position(COVER_EP) == UNKNOWN_POSITION
        d.zcl_cover_stop(COVER_EP)
        assert d.zcl_cover_get_position(COVER_EP) == UNKNOWN_POSITION

        d.step_time(MINIMUM_SWITCH_TIME_MS)
        d.zcl_cover_open(COVER_EP)
        d.step_time(TRAVEL_MS + OVERRUN_MS)
        assert d.zcl_cover_get_moving(COVER_EP) == ZCL_WINDOW_COVERING_MOVING_STOPPED
        assert d.zcl_cover_get_position(COVER_EP) == 0


@pytest.mark.parametrize("attr,value", [
    (ZCL_ATTR_WINDOW_COVERING_OPEN_TIME, 429497),
    (ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME, 429497),
    (ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE, 10000),
    (ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE, 10000),
])
def test_out_of_range_cover_setting_write_fails(cover_device: Device, attr: int, value: int):
    d = cover_device
    before = d.read_zigbee_attr(COVER_EP, ZCL_CLUSTER_WINDOW_COVERING, attr)
    events = []
    d.p.on_event.append(events.append)

    res = d.p.exec(f"zcl_write {COVER_EP} 0x{ZCL_CLUSTER_WINDOW_COVERING:04X} "
                   f"0x{attr:04X} {value}")

    assert not res.ok
    assert d.read_zigbee_attr(COVER_EP, ZCL_CLUSTER_WINDOW_COVERING, attr) == before
    assert not any(evt.kind == "zcl_attr_change" for evt in events)
