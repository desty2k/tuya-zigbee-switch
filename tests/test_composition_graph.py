import pytest

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_BASIC_MFR_NAME,
    ZCL_ATTR_DEVICE_CONFIG_STATUS,
    ZCL_ATTR_COVER_SWITCH_SWITCH_TYPE,
    ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE,
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
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
