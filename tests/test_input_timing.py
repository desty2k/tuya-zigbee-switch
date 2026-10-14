"""Input pipeline timing: clock wrap, edge-queue overflow and late edges."""

import pytest
from client import StubProc
from conftest import Device

HAL_ZIGBEE_NETWORK_JOINED = 1
ONBOARD_HOLD_MS = 2000

# First virtual millisecond at which hal_millis() wraps on each platform model:
# Telink's 32-bit 16 MHz tick overflows at 2^32 / 16000 = 268435.456 ms.
WRAP_AT_MS = {"telink": 268_436, "native": 2**32}


def gestures(device: Device) -> list[dict[str, str]]:
    return [event.payload for event in device._events if event.kind == "gesture"]


def button_events(device: Device) -> list[dict[str, str]]:
    return [event.payload for event in device._events if event.kind == "btn_event"]


def test_native_time_model_wraps_at_2_pow_32() -> None:
    with StubProc(device_config="A;B;") as proc:
        device = Device(proc)
        device.time_set(2**32 - 1)
        assert device.millis() == 2**32 - 1
        device.step_time(2)
        assert device.millis() == 1


def test_time_set_rejects_moving_backwards() -> None:
    with StubProc(device_config="A;B;") as proc:
        device = Device(proc)
        device.time_set(1000)
        assert not proc.exec("time_set 10").ok


def test_pin_burst_and_pin_late_drive_the_input() -> None:
    with StubProc(device_config="A;B;SA0u;") as proc:
        device = Device(proc)
        device.pin_burst("A0", 3)
        assert device.counters()["gpio_edges_captured"] == 3
        device.pin_late("A0", 1, 20)
        assert device.counters()["gpio_edges_captured"] == 4
        assert device.millis() == 20
