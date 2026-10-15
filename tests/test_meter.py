"""BL0937 meter on the TS011F outlet board: pulse trains in, ZCL quantities out.

The stub's pulse generators feed the counters only, as BL0937 CF/CF1 outputs
reach the TLSR8258 timers. CF1 carries voltage while SEL (D2) is high and
current while it is low.
"""

import pytest

from client import StubProc
from conftest import Device
from management_protocol_consts import (
    ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR,
    ZCL_CLUSTER_DEVICE_CONFIG,
)
from zcl_consts import (
    ZCL_ATTR_ELEC_MEAS_AC_CURRENT_DIVISOR,
    ZCL_ATTR_ELEC_MEAS_AC_POWER_DIVISOR,
    ZCL_ATTR_ELEC_MEAS_AC_VOLTAGE_DIVISOR,
    ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER,
    ZCL_ATTR_ELEC_MEAS_RMS_CURRENT,
    ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE,
    ZCL_ATTR_METER_CALIBRATION_POWER_GAIN,
    ZCL_ATTR_METER_CALIBRATION_VOLTAGE_GAIN,
    ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED,
    ZCL_ATTR_METERING_DIVISOR,
    ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
    ZCL_CLUSTER_METER_CALIBRATION,
    ZCL_CLUSTER_METERING,
    ZCL_CMD_METER_CALIBRATION_RESET_ENERGY,
)

CONFIG = "drzlxjne;TS011F-BS-PM-3;SB4u;RB5;IC4;EBC2C3D2;M;"
EM = ZCL_CLUSTER_ELECTRICAL_MEASUREMENT
# Board scale: 120670 uV/Hz, 9050 uA/Hz, 1063500 uW/Hz.
V_PER_HZ, A_PER_HZ, W_PER_HZ = 0.12067, 0.00905, 1.0635
# Writes are at most every 5 min, after 50 Wh or once an hour.
CHECKPOINT_MIN_S = 300


def load(device: Device, volts: float, amps: float, watts: float) -> None:
    """Pulse rates the BL0937 of the board outputs for a load."""
    for args in (f"C2 {round(watts / W_PER_HZ * 1000)}",
                 f"C3 {round(volts / V_PER_HZ * 1000)} D2 1",
                 f"C3 {round(amps / A_PER_HZ * 1000)} D2 0"):
        assert device.p.exec(f"pulse_rate {args}").ok


def em(device: Device, attr: int) -> int:
    return int(device.read_zigbee_attr(1, EM, attr))


def energy_wh(device: Device) -> int:
    return int(device.read_zigbee_attr(1, ZCL_CLUSTER_METERING,
                                       ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED))


def run_seconds(device: Device, seconds: int) -> None:
    for _ in range(seconds):
        device.step_time(1000)


@pytest.fixture
def device_config() -> str:
    return CONFIG


@pytest.fixture
def meter(device: Device) -> Device:
    device.freeze_time()
    return device


def test_quantities_are_invalid_until_measured(meter: Device) -> None:
    assert em(meter, ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE) == 0xFFFF
    assert em(meter, ZCL_ATTR_ELEC_MEAS_RMS_CURRENT) == 0xFFFF
    assert em(meter, ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER) == -32768
    assert em(meter, ZCL_ATTR_ELEC_MEAS_AC_VOLTAGE_DIVISOR) == 10
    assert em(meter, ZCL_ATTR_ELEC_MEAS_AC_CURRENT_DIVISOR) == 1000
    assert em(meter, ZCL_ATTR_ELEC_MEAS_AC_POWER_DIVISOR) == 1
    assert int(meter.read_zigbee_attr(1, ZCL_CLUSTER_METERING, ZCL_ATTR_METERING_DIVISOR)) == 1000


@pytest.mark.parametrize("volts,amps,watts", [(237.0, 0.0317, 6.8), (230.0, 10.0, 2300.0),
                                              (250.0, 16.0, 3680.0)])
def test_pulse_trains_give_voltage_current_and_power(meter: Device, volts, amps, watts) -> None:
    load(meter, volts, amps, watts)
    meter.clear_events()
    run_seconds(meter, 15)
    assert em(meter, ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE) == pytest.approx(volts * 10, abs=2)
    assert em(meter, ZCL_ATTR_ELEC_MEAS_RMS_CURRENT) == pytest.approx(amps * 1000, rel=0.03, abs=2)
    assert em(meter, ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER) == pytest.approx(watts, rel=0.02, abs=1)
    for attr in (ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE, ZCL_ATTR_ELEC_MEAS_RMS_CURRENT,
                 ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER):
        meter.wait_for_attr_change(1, EM, attr)


def test_energy_accumulates_and_survives_reboot() -> None:
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        device.freeze_time()
        load(device, 230.0, 16.0, 3680.0)
        run_seconds(device, CHECKPOINT_MIN_S + 1)
        # 3680 W for 301 s = 307.7 Wh.
        assert energy_wh(device) == pytest.approx(307, abs=2)
        device.wait_for_attr_change(1, ZCL_CLUSTER_METERING,
                                    ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED)
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        device.freeze_time()
        assert energy_wh(device) == pytest.approx(307, abs=2)
        load(device, 230.0, 0.0, 1000.0)
        run_seconds(device, 36)
        assert energy_wh(device) == pytest.approx(317, abs=2)


def test_reset_energy_command(meter: Device) -> None:
    load(meter, 230.0, 16.0, 3680.0)
    run_seconds(meter, 10)
    assert energy_wh(meter) > 0
    meter.call_zigbee_cmd(1, ZCL_CLUSTER_METER_CALIBRATION, ZCL_CMD_METER_CALIBRATION_RESET_ENERGY)
    assert energy_wh(meter) == 0
    meter.call_zigbee_cmd(1, ZCL_CLUSTER_METER_CALIBRATION, ZCL_CMD_METER_CALIBRATION_RESET_ENERGY,
                          payload=b"\x01", expected_result="MALFORMED_COMMAND")


def test_calibration_scales_readings_and_survives_reboot() -> None:
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        device.freeze_time()
        assert int(device.read_zigbee_attr(1, ZCL_CLUSTER_METER_CALIBRATION,
                                           ZCL_ATTR_METER_CALIBRATION_VOLTAGE_GAIN)) == 1000000
        device.write_zigbee_attr(1, ZCL_CLUSTER_METER_CALIBRATION,
                                 ZCL_ATTR_METER_CALIBRATION_VOLTAGE_GAIN, 1010000)
        # Outside 0.5..1.5 is refused and leaves the gain.
        assert not proc.exec(f"zcl_write 1 0x{ZCL_CLUSTER_METER_CALIBRATION:04X} "
                             f"0x{ZCL_ATTR_METER_CALIBRATION_POWER_GAIN:04X} 2000000").ok
        assert int(device.read_zigbee_attr(1, ZCL_CLUSTER_METER_CALIBRATION,
                                           ZCL_ATTR_METER_CALIBRATION_POWER_GAIN)) == 1000000
        load(device, 230.0, 1.0, 230.0)
        run_seconds(device, 15)
        assert em(device, ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE) == pytest.approx(2323, abs=2)
    with StubProc(device_config=CONFIG) as proc:
        device = Device(proc)
        device.freeze_time()
        assert int(device.read_zigbee_attr(1, ZCL_CLUSTER_METER_CALIBRATION,
                                           ZCL_ATTR_METER_CALIBRATION_VOLTAGE_GAIN)) == 1010000
        load(device, 230.0, 1.0, 230.0)
        run_seconds(device, 15)
        assert em(device, ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE) == pytest.approx(2323, abs=2)


@pytest.mark.parametrize("tokens", ["SC2u;EBC2C3D2", "RD2;EBC2C3D2", "IC3;EBC2C3D2",
                                    "EBC2C3D2;EBA0A1A2"])
def test_meter_refuses_claimed_pins_and_missing_counters(tokens: str) -> None:
    with StubProc(device_config=f"M;PM;{tokens};") as proc:
        device = Device(proc)
        # Parse (pin conflict) or resource error: the recovery graph, no meter.
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR) in ("2", "3")
        assert "0x0B04" not in proc.exec("zcl_clusters").payload["clusters"]


def test_meter_pulses_stay_out_of_the_button_pipeline(meter: Device) -> None:
    before = meter.counters()
    load(meter, 230.0, 16.0, 3680.0)
    run_seconds(meter, 5)
    after = meter.counters()
    for key in ("gpio_irq_count", "gpio_edges_captured", "button_events_emitted"):
        assert after[key] == before[key], key
    # The button still drives the relay.
    relay = meter.get_gpio("B5", refresh=True)
    meter.click_button("B4")
    meter.step_time(100)
    assert meter.get_gpio("B5", refresh=True) is not relay


def test_switch_only_image_rejects_the_meter_token() -> None:
    with StubProc(cmd=["./build/stub/stub_switch_only"], device_config=CONFIG) as proc:
        device = Device(proc)
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR) == "8"
