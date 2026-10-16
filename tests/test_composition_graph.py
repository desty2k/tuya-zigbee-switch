import pytest

from client import StubProc
from conftest import Device
from management_protocol_consts import (
    ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR,
    ZCL_ATTR_DEVICE_CONFIG_TEXT,
)
from zcl_consts import (
    ZCL_ATTR_BASIC_MFR_NAME,
    ZCL_ATTR_BASIC_MODEL_ID,
    ZCL_ATTR_DEVICE_CONFIG_STATUS,
    ZCL_ATTR_COVER_SWITCH_SWITCH_TYPE,
    ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE,
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_ATTR_START_UP_ONOFF,
    ZCL_ATTR_WINDOW_COVERING_MOVING,
    ZCL_CLUSTER_BASIC,
    ZCL_CLUSTER_DEVICE_CONFIG,
    ZCL_CLUSTER_COVER_SWITCH_CONFIG,
    ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
    ZCL_CLUSTER_WINDOW_COVERING,
)


TS0726_4GANG_CONFIG = (
    "pzao9ls1;BS4;LC1;SD3u;RA0;ID2;SD7u;RA1;IB4;SD4u;RC2;IB5;SB6u;RC3;IC4;M;"
)


@pytest.mark.parametrize(
    "config, attributes",
    [
        (
            "M;Switch;SA0u;RB0;",
            [
                (1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MFR_NAME),
                (1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE),
                (2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF),
            ],
        ),
        (
            "M;Relay;RA0;",
            [
                (1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MFR_NAME),
                (1, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF),
            ],
        ),
        (
            "M;Cover;XA0A1u;CB0B1;",
            [
                (1, ZCL_CLUSTER_COVER_SWITCH_CONFIG, ZCL_ATTR_COVER_SWITCH_SWITCH_TYPE),
                (1, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC, ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE),
                (2, ZCL_CLUSTER_WINDOW_COVERING, ZCL_ATTR_WINDOW_COVERING_MOVING),
            ],
        ),
        (
            "M;Remote;SA0u;",
            [
                (1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MFR_NAME),
                (1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE),
            ],
        ),
        ("M;Empty;", [(1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MFR_NAME)]),
    ],
)
def test_composition_graph_snapshot(
    config: str, attributes: list[tuple[int, int, int]]
) -> None:
    proc = StubProc(device_config=config).start()
    try:
        device = Device(proc)
        for endpoint, cluster, attribute in attributes:
            assert device.read_zigbee_attr(endpoint, cluster, attribute) is not None
    finally:
        proc.stop()


def test_ts0726_4gang_graph_snapshot_preserves_endpoint_order() -> None:
    source = TS0726_4GANG_CONFIG
    proc = StubProc(device_config=source).start()
    try:
        device = Device(proc)
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MFR_NAME) == "pzao9ls1"
        for endpoint in range(1, 5):
            assert (
                device.read_zigbee_attr(
                    endpoint,
                    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
                    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
                )
                == "1"
            )
        for endpoint in range(5, 9):
            assert device.read_zigbee_attr(endpoint, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF) == "0"
    finally:
        proc.stop()
    assert source == TS0726_4GANG_CONFIG


def test_invalid_composition_enters_management_only_recovery() -> None:
    proc = StubProc(device_config="M;Invalid;SA;").start()
    try:
        device = Device(proc)
        counters = device.counters()
        assert counters["gpio_init_count"] == 0
        assert counters["gpio_write_count"] == 0
        assert counters["tasks_active"] == 0
        assert proc.exec("zcl_read 1 0x0000 0x0004").ok
        assert (
            device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                    ZCL_ATTR_DEVICE_CONFIG_STATUS)
            == "2"
        )
    finally:
        proc.stop()


def _registered_clusters(proc: StubProc) -> dict[tuple[int, int], str]:
    res = proc.exec("zcl_clusters")
    assert res.ok, res.payload
    clusters = {}
    for entry in res.payload["clusters"].split(","):
        endpoint, cluster, role, _manufacturer = entry.split(":")
        clusters[(int(endpoint), int(cluster, 16))] = role
    return clusters


def test_switch_and_cover_switch_endpoints_only_send_on_off_level_and_covering() -> None:
    """Clusters an endpoint only sends from are clients, as on `main`."""
    proc = StubProc(device_config="M;Mixed;SA2u;RB2;XA0A1u;CB0B1;").start()
    try:
        assert _registered_clusters(proc) == {
            (1, 0x0000): "S",
            (1, 0xFC10): "S",
            (1, 0xFC11): "S",
            (1, 0x0019): "C",
            # switch endpoint
            (1, 0x0007): "S",
            (1, 0x0006): "C",
            (1, 0x0012): "S",
            (1, 0xFC02): "S",
            (1, 0x0008): "C",
            # relay endpoint
            (2, 0x0006): "S",
            (2, 0x0008): "S",
            (2, 0x0004): "S",
            # cover switch endpoint
            (3, 0xFC01): "S",
            (3, 0x0102): "C",
            (3, 0x0012): "S",
            (3, 0xFC02): "S",
            # cover endpoint
            (4, 0x0102): "S",
        }
    finally:
        proc.stop()


# device_config_status_t / device_config_error_t in src/zigbee/management_protocol.h
DEVICE_CONFIG_STATUS_ERROR = 2
DEVICE_CONFIG_ERROR_PARSE = 2
DEVICE_CONFIG_ERROR_RESOURCE = 3

GRAPH_FIELDS = (
    "endpoints",
    "server_clusters",
    "client_clusters",
    "attributes",
    "zcl_registrations",
)


def _graph_usage(proc: StubProc) -> dict[str, int]:
    res = proc.exec("zcl_graph_usage")
    assert res.ok, res.payload
    return {key: int(value) for key, value in res.payload.items()}


def test_ts0726_4gang_registers_within_telink_limits_and_relay_4_responds() -> None:
    proc = StubProc(device_config=TS0726_4GANG_CONFIG).start()
    try:
        device = Device(proc)
        usage = _graph_usage(proc)
        assert usage["endpoints"] == 8
        # 35 HAL clusters plus the SDK's own OTA registration.
        assert usage["zcl_registrations"] == 36
        assert usage["zcl_registrations"] <= usage["max_zcl_registrations"]
        assert usage["server_clusters"] <= usage["max_server_clusters"]
        assert usage["attributes"] <= usage["max_attributes"]

        relay_4_pin = "C3"
        assert device.get_gpio(relay_4_pin, refresh=True) is False
        device.zcl_relay_on(8)
        device.step_time(10)
        assert device.zcl_relay_get(8) == "1"
        assert device.get_gpio(relay_4_pin, refresh=True) is True
    finally:
        proc.stop()


@pytest.mark.parametrize(
    "config",
    [
        TS0726_4GANG_CONFIG,
        "M;Relays;IA4;RA0;RA1;",
        "M;Cover;XA0A1u;CB0B1;SA2u;RB2;",
        "M;Battery;BTA5;SA0u;",
        "M;Empty;",
    ],
)
def test_preflight_counts_exactly_the_registered_graph(config: str) -> None:
    proc = StubProc(device_config=config).start()
    try:
        usage = _graph_usage(proc)
        assert usage["preflight_parsed"] == 1
        for field in GRAPH_FIELDS:
            assert usage[field] == usage[f"preflight_{field}"], field
    finally:
        proc.stop()


# hal_gpio_pull_t in src/hal/gpio.h
HAL_GPIO_PULL_DOWN = 3


def _assert_recovery_graph(proc: StubProc) -> None:
    device = Device(proc)
    assert device.read_zigbee_attr(1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MODEL_ID) == "recovery"
    assert device.read_zigbee_attr(
        1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_STATUS
    ) == str(DEVICE_CONFIG_STATUS_ERROR)
    assert set(_registered_clusters(proc)) == {
        (1, 0x0000),
        (1, 0xFC10),
        (1, 0xFC11),
        (1, 0x0019),
    }


@pytest.mark.parametrize(
    "config, error",
    [
        # Nine endpoints: Telink holds eight.
        ("M;Nine;RA0;RA1;RA2;RA3;RA4;RA5;RA6;RA7;RB0;", DEVICE_CONFIG_ERROR_PARSE),
        # Eight switch endpoints take 169 attributes: the tables hold 160.
        ("M;Eight;SA0u;SA1u;SA2u;SA3u;SA4u;SA5u;SA6u;SA7u;", DEVICE_CONFIG_ERROR_RESOURCE),
    ],
)
def test_config_over_platform_limits_falls_back_to_recovery_graph(
    config: str, error: int
) -> None:
    proc = StubProc(device_config=config).start()
    try:
        _assert_recovery_graph(proc)
        device = Device(proc)
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_DEVICE_CONFIG, ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR
        ) == str(error)
        usage = _graph_usage(proc)
        assert usage["endpoints"] == 1
        assert usage["zcl_registrations"] <= usage["max_zcl_registrations"]
    finally:
        proc.stop()


def test_config_write_over_platform_limits_is_rejected() -> None:
    proc = StubProc(device_config="M;Relay;RA0;").start()
    try:
        config = "M;Eight;SA0u;SA1u;SA2u;SA3u;SA4u;SA5u;SA6u;SA7u;"
        data = len(config).to_bytes(2, "little") + config.encode()
        res = proc.exec(
            f"zcl_write_raw 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} {data.hex()}"
        )
        assert not res.ok
    finally:
        proc.stop()


def test_refused_zigbee_init_leaves_relays_off_and_inputs_inactive() -> None:
    config = "M;Safe;SA0u;RB0;"
    with StubProc(device_config=config) as proc:
        device = Device(proc)
        device.write_zigbee_attr(2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_START_UP_ONOFF, 1)

    # The relay starts ON from its saved startup mode while the graph is wired.
    with StubProc(device_config=config) as proc:
        assert Device(proc).get_gpio("B0", refresh=True) is True

    with StubProc(
        cmd=["./build/stub/stub_device", "--fail-zigbee-init"], device_config=config
    ) as proc:
        device = Device(proc)
        _assert_recovery_graph(proc)
        assert device.get_gpio("B0", refresh=True) is False
        device.click_button("A0")
        device.step_time(1000)
        assert device.get_gpio("B0", refresh=True) is False


def test_refused_zigbee_init_keeps_wired_relays_driven_and_rests_the_rest() -> None:
    with StubProc(
        cmd=["./build/stub/stub_device", "--fail-zigbee-init"], device_config="M;Safe;SA0u;RB0;"
    ) as proc:
        device = Device(proc)
        _assert_recovery_graph(proc)
        # The wired relay stays an output driven off; every other pin,
        # including the wired switch input, rests on the pull-down.
        assert device.pin_state("B0") == {"initialized": 1, "input": 0, "pull": 0, "value": 0}
        for pin in ("A0", "A1", "C0", "D3"):
            assert device.pin_state(pin) == {
                "initialized": 1, "input": 1, "pull": HAL_GPIO_PULL_DOWN, "value": 0,
            }, pin
