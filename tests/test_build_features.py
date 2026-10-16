"""Config tokens for features a firmware image does not contain.

`stub_switch_only` is built with FEATURES="relay switch", the feature set of
a single-relay board; `stub_device` contains every feature.
"""

import pytest

from client import StubProc
from conftest import Device
from management_protocol_consts import (
    ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR,
    ZCL_ATTR_DEVICE_CONFIG_STATUS,
    ZCL_ATTR_DEVICE_CONFIG_TEXT,
    ZCL_CLUSTER_DEVICE_CONFIG,
)
from zcl_consts import (
    ZCL_ATTR_BASIC_MODEL_ID,
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_INCHING_DURATION,
    ZCL_ATTR_ONOFF_INTERLOCK_GROUP,
    ZCL_CLUSTER_BASIC,
    ZCL_CLUSTER_ON_OFF,
)

SWITCH_ONLY_CMD = ["./build/stub/stub_switch_only"]

# device_config_status_t / device_config_error_t in src/zigbee/management_protocol.h
DEVICE_CONFIG_STATUS_READY = 0
DEVICE_CONFIG_STATUS_ERROR = 2
DEVICE_CONFIG_ERROR_NONE = 0
DEVICE_CONFIG_ERROR_FEATURE_MISSING = 8

TS0726_4GANG_CONFIG = (
    "pzao9ls1;BS4;LC1;SD3u;RA0;ID2;SD7u;RA1;IB4;SD4u;RC2;IB5;SB6u;RC3;IC4;M;"
)

# hal_gpio_pull_t in src/hal/gpio.h
HAL_GPIO_PULL_UP = 1
HAL_GPIO_PULL_DOWN = 3

# Every TLSR8258 pin hal_gpio_rest_all() holds: ports A-D without PA7 (SWS).
TLSR8258_REST_PINS = [f"{port}{index}" for port in "ABCD" for index in range(8)
                      if (port, index) != ("A", 7)]

# Shutter whose motor relays C0/D3 floated and chattered under an image
# without the cover feature.
TS130F_CONFIG = "vsj5o7dm;TS130F-BS-CVR;LB4i;XB6A1A0u;CD3C0;IC3;IB5;PD2;M;"

EXCLUDED_FEATURE_CONFIGS = [
    "M;Cover;CB0B1;",
    "M;CoverSwitch;XA0A1u;RB0;",
    "M;Battery;BTA5;SA0u;",
    "M;LateCover;SA0u;RB2;CB0B1;",
    "M;Interlock;SA0u;RB2;RB3;K03;",
]


def _config_state(device: Device) -> tuple[str, str]:
    return (
        device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_STATUS),
        device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR),
    )


def test_switch_only_image_runs_the_ts0726_4gang_config() -> None:
    with StubProc(cmd=SWITCH_ONLY_CMD, device_config=TS0726_4GANG_CONFIG) as proc:
        device = Device(proc)
        assert _config_state(device) == (
            str(DEVICE_CONFIG_STATUS_READY),
            str(DEVICE_CONFIG_ERROR_NONE),
        )
        device.zcl_relay_on(8)
        device.step_time(10)
        assert device.read_zigbee_attr(8, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF) == "1"
        assert device.get_gpio("C3", refresh=True) is True


def test_image_without_interlock_has_no_interlock_group_attribute() -> None:
    with StubProc(cmd=SWITCH_ONLY_CMD, device_config="M;Relay;SA0u;RB0;") as proc:
        device = Device(proc)
        # The relay follows the switch endpoint.
        read = proc.exec(
            f"zcl_read 2 0x{ZCL_CLUSTER_ON_OFF:04X} 0x{ZCL_ATTR_ONOFF_INTERLOCK_GROUP:04X}"
        )
        assert not read.ok
        assert not proc.exec(
            f"zcl_write 2 0x{ZCL_CLUSTER_ON_OFF:04X} "
            f"0x{ZCL_ATTR_ONOFF_INTERLOCK_GROUP:04X} 0"
        ).ok
        assert device.read_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INCHING_DURATION
        ) == "0"
        device.zcl_relay_on(2)
        device.step_time(10)
        assert device.zcl_relay_get(2) == "1"


@pytest.mark.parametrize("config", EXCLUDED_FEATURE_CONFIGS)
def test_token_for_excluded_feature_boots_recovery_graph(config: str) -> None:
    with StubProc(cmd=SWITCH_ONLY_CMD, device_config=config) as proc:
        device = Device(proc)
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MODEL_ID
        ) == "recovery"
        assert _config_state(device) == (
            str(DEVICE_CONFIG_STATUS_ERROR),
            str(DEVICE_CONFIG_ERROR_FEATURE_MISSING),
        )
        # Relays named before the rejected token stay off.
        assert device.get_gpio("B2", refresh=True) is False


@pytest.mark.parametrize("config", EXCLUDED_FEATURE_CONFIGS + [TS130F_CONFIG])
def test_recovery_graph_rests_every_pin_on_the_pull_down(config: str) -> None:
    with StubProc(cmd=SWITCH_ONLY_CMD, device_config=config) as proc:
        device = Device(proc)
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MODEL_ID
        ) == "recovery"
        assert _config_state(device) == (
            str(DEVICE_CONFIG_STATUS_ERROR),
            str(DEVICE_CONFIG_ERROR_FEATURE_MISSING),
        )
        for pin in TLSR8258_REST_PINS:
            assert device.pin_state(pin) == {
                "initialized": 1, "input": 1, "pull": HAL_GPIO_PULL_DOWN, "value": 0,
            }, pin
        # SWS stays as the SDK left it.
        assert device.pin_state("A7")["initialized"] == 0


def test_normal_boot_leaves_unclaimed_pins_untouched() -> None:
    with StubProc(cmd=SWITCH_ONLY_CMD, device_config="M;Relay;SA0u;RB0;") as proc:
        device = Device(proc)
        assert device.pin_state("A0") == {
            "initialized": 1, "input": 1, "pull": HAL_GPIO_PULL_UP, "value": 1,
        }
        assert device.pin_state("B0")["input"] == 0
        for pin in TLSR8258_REST_PINS:
            if pin not in ("A0", "B0"):
                assert device.pin_state(pin)["initialized"] == 0, pin


@pytest.mark.parametrize("config", EXCLUDED_FEATURE_CONFIGS)
def test_full_image_accepts_every_feature_token(config: str) -> None:
    with StubProc(device_config=config) as proc:
        device = Device(proc)
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MODEL_ID
        ) != "recovery"
        assert _config_state(device) == (
            str(DEVICE_CONFIG_STATUS_READY),
            str(DEVICE_CONFIG_ERROR_NONE),
        )


def test_config_write_with_excluded_feature_is_rejected() -> None:
    with StubProc(cmd=SWITCH_ONLY_CMD, device_config="M;Relay;SA0u;RB0;") as proc:
        device = Device(proc)
        config = "M;Cover;CB0B1;"
        data = len(config).to_bytes(2, "little") + config.encode()
        res = proc.exec(
            f"zcl_write_raw 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} {data.hex()}"
        )
        # Validation refuses the write without touching the running config.
        assert not res.ok
        record = (
            ZCL_ATTR_DEVICE_CONFIG_TEXT.to_bytes(2, "little") + bytes([0x44]) + data
        )
        res = proc.exec(
            f"zcl_foundation 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} 0x02 {record.hex()}"
        )
        assert res.payload["result"] == "INVALID_VALUE"
        assert _config_state(device) == (
            str(DEVICE_CONFIG_STATUS_READY),
            str(DEVICE_CONFIG_ERROR_NONE),
        )
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_TEXT
        ) == "M;Relay;SA0u;RB0;"
