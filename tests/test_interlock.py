from collections.abc import Iterator
from pathlib import Path

import pytest

from tests.client import StubProc
from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF_INTERLOCK_GROUP,
    ZCL_ATTR_ONOFF_INCHING_DURATION,
    ZCL_ATTR_START_UP_ONOFF,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CMD_ONOFF_TOGGLE,
    ZCL_CMD_ON_WITH_TIMED_OFF,
)

NVM_DIR = Path("./stub_nvm_data")
NV_ITEM_RELAY_STATE_BASE = 40
# relay_state_record_t: on_off, startup_mode, indicator_mode,
# indicator_state, inching_ms (uint16), interlock_group, padding.
RELAY_RECORD_INTERLOCK_OFFSET = 6
START_UP_ON = 1
DEFAULT_DEAD_TIME_MS = 200


def start_device(config: str) -> Iterator[Device]:
    with StubProc(device_config=config) as proc:
        yield Device(proc)


@pytest.fixture()
def grouped_relays() -> Iterator[Device]:
    yield from start_device("X;Y;RA0;RA1;RA2;K07:0;")


def timed_off_payload(on_time_ds: int) -> bytes:
    return bytes((0, on_time_ds & 0xFF, on_time_ds >> 8, 0, 0))


def test_zigbee_on_and_toggle_keep_one_group_member_on(
    grouped_relays: Device,
) -> None:
    grouped_relays.zcl_relay_on(1)
    grouped_relays.zcl_relay_on(2)
    assert grouped_relays.zcl_relay_get(1) == "0"
    assert grouped_relays.zcl_relay_get(2) == "1"

    grouped_relays.call_zigbee_cmd(3, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE)
    assert grouped_relays.zcl_relay_get(2) == "0"
    assert grouped_relays.zcl_relay_get(3) == "1"


def test_inching_and_timed_on_respect_group(grouped_relays: Device) -> None:
    grouped_relays.zcl_relay_on(1)
    grouped_relays.write_zigbee_attr(
        2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 500
    )
    grouped_relays.zcl_relay_on(2)
    assert grouped_relays.zcl_relay_get(1) == "0"
    assert grouped_relays.zcl_relay_get(2) == "1"
    grouped_relays.step_time(500)
    assert grouped_relays.zcl_relay_get(2) == "0"

    grouped_relays.zcl_relay_on(1)
    grouped_relays.call_zigbee_cmd(
        3,
        ZCL_CLUSTER_ON_OFF,
        ZCL_CMD_ON_WITH_TIMED_OFF,
        timed_off_payload(10),
    )
    assert grouped_relays.zcl_relay_get(1) == "0"
    assert grouped_relays.zcl_relay_get(3) == "1"
    grouped_relays.step_time(1000)
    assert grouped_relays.zcl_relay_get(3) == "0"


def test_button_requests_respect_group() -> None:
    with StubProc(device_config="X;Y;SA0u;SA1u;RA2;RA3;K03:0;") as proc:
        device = Device(proc)
        device.press_button("A0")
        assert device.zcl_relay_get(3) == "1"
        device.press_button("A1")
        assert device.zcl_relay_get(3) == "0"
        assert device.zcl_relay_get(4) == "1"


def relay_record(relay_idx: int) -> Path:
    return NVM_DIR / f"item_{NV_ITEM_RELAY_STATE_BASE + relay_idx:02x}.bin"


def test_interlock_attribute_accepts_only_config_groups() -> None:
    # Relays 0-1 form K group 1, relay 2 is free, relays 3-4 drive a cover
    # whose implicit group is 2.
    with StubProc(device_config="X;Y;RA0;RA1;RA2;K03:0;CB0B1;") as proc:
        device = Device(proc)

        def write(endpoint: int, group: int) -> bool:
            return proc.exec(
                f"zcl_write {endpoint} 0x{ZCL_CLUSTER_ON_OFF:04X} "
                f"0x{ZCL_ATTR_ONOFF_INTERLOCK_GROUP:04X} {group}"
            ).ok

        def group_of(endpoint: int) -> str:
            return device.read_zigbee_attr(
                endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INTERLOCK_GROUP
            )

        assert not write(3, 2)  # cover group
        assert not write(3, 7)  # undefined group
        assert not write(1, 0)  # config-defined membership is fixed
        assert group_of(1) == "1"
        assert group_of(3) == "0"

        assert write(3, 1)
        device.zcl_relay_on(1)
        device.zcl_relay_on(3)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(3) == "1"


def test_config_interlock_wins_over_persisted_group() -> None:
    config = "X;Y;RA0;RA1;K03:0;"
    with StubProc(device_config=config) as proc:
        device = Device(proc)
        for endpoint in (1, 2):
            device.write_zigbee_attr(
                endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_START_UP_ONOFF, START_UP_ON
            )
    # A record that names no group, as saved before the K token existed.
    for relay_idx in (0, 1):
        record = bytearray(relay_record(relay_idx).read_bytes())
        record[RELAY_RECORD_INTERLOCK_OFFSET] = 0
        relay_record(relay_idx).write_bytes(bytes(record))

    with StubProc(device_config=config) as proc:
        device = Device(proc)
        device.step_time(10)
        states = [device.zcl_relay_get(endpoint) for endpoint in (1, 2)]
        assert states.count("1") <= 1, states
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INTERLOCK_GROUP
        ) == "1"


def test_relay_settings_reset_when_relay_topology_changes() -> None:
    def configure(config: str) -> None:
        with StubProc(device_config=config) as proc:
            device = Device(proc)
            device.write_zigbee_attr(
                1, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_START_UP_ONOFF, START_UP_ON
            )
            device.write_zigbee_attr(
                1, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 500
            )

    def settings(config: str) -> tuple[str, str]:
        with StubProc(device_config=config) as proc:
            device = Device(proc)
            return (
                device.read_zigbee_attr(1, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_START_UP_ONOFF),
                device.read_zigbee_attr(
                    1, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION
                ),
            )

    configure("X;Y;RA0;RA1;")
    # A token that does not touch the relays keeps the relay settings.
    assert settings("X;Y;RA0;RA1;G400;") == (str(START_UP_ON), "500")
    # Adding an interlock group changes the relay topology.
    assert settings("X;Y;RA0;RA1;K03;") != (str(START_UP_ON), "500")

    configure("X;Y;RA0;RA1;K03;")
    # Retuning the dead time or repinning a relay keeps the relay settings.
    assert settings("X;Y;RA0;RA1;K03:500;") == (str(START_UP_ON), "500")
    assert settings("X;Y;RA0;RA2;K03:500;") == (str(START_UP_ON), "500")


def test_dead_time_defaults_when_k_has_no_duration() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;K03;") as proc:
        device = Device(proc)
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(2) == "0"
        device.step_time(DEFAULT_DEAD_TIME_MS - 1)
        assert device.zcl_relay_get(2) == "0"
        device.step_time(1)
        assert device.zcl_relay_get(2) == "1"


def test_second_toggle_during_dead_time_cancels_pending_on() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;K03:50;") as proc:
        device = Device(proc)
        device.zcl_relay_on(1)
        device.call_zigbee_cmd(2, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE)
        device.step_time(20)
        device.call_zigbee_cmd(2, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE)
        device.step_time(50)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(2) == "0"


def test_dead_time_defers_new_target_and_replaces_pending() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;RA2;K07:50;") as proc:
        device = Device(proc)
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(2) == "0"
        device.step_time(20)
        device.zcl_relay_on(3)
        device.step_time(49)
        assert device.zcl_relay_get(2) == "0"
        assert device.zcl_relay_get(3) == "0"
        device.step_time(1)
        assert device.zcl_relay_get(2) == "0"
        assert device.zcl_relay_get(3) == "1"


def test_off_cancels_pending_target() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;K03:50;") as proc:
        device = Device(proc)
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        device.zcl_relay_off(2)
        device.step_time(50)
        assert device.zcl_relay_get(1) == "0"
        assert device.zcl_relay_get(2) == "0"


def test_deferred_inching_starts_duration_after_dead_time() -> None:
    with StubProc(device_config="X;Y;RA0;RA1;K03:50;") as proc:
        device = Device(proc)
        device.write_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION, 500
        )
        device.zcl_relay_on(1)
        device.zcl_relay_on(2)
        device.step_time(50)
        assert device.zcl_relay_get(2) == "1"
        device.step_time(499)
        assert device.zcl_relay_get(2) == "1"
        device.step_time(1)
        assert device.zcl_relay_get(2) == "0"
