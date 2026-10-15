from tests.client import StubProc
from tests.conftest import Device
from tests.management_protocol_consts import (
    ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR,
    ZCL_ATTR_DEVICE_CONFIG_REVISION,
    ZCL_ATTR_DEVICE_CONFIG_STATUS,
    ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE,
    ZCL_ATTR_DEVICE_CONFIG_TEXT,
    ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET,
    ZCL_ATTR_DIAGNOSTICS_CAPABILITIES,
    ZCL_ATTR_DIAGNOSTICS_UPTIME_MS,
    ZCL_CLUSTER_DEVICE_CONFIG,
    ZCL_CLUSTER_DEVICE_DIAGNOSTICS,
)


def test_management_clusters_are_fixed_on_empty_composition() -> None:
    with StubProc(device_config="A;B;") as process:
        device = Device(process)
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_TEXT) == "A;B;"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_DIAGNOSTICS,
                                       ZCL_ATTR_DIAGNOSTICS_CAPABILITIES) == "1"


def test_invalid_config_keeps_management_recovery_available() -> None:
    with StubProc(device_config="invalid") as process:
        device = Device(process)
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_STATUS) == "2"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_LAST_ERROR) == "2"


def test_config_apply_persists_revision_and_schedules_reboot() -> None:
    with StubProc(device_config="A;B;") as process:
        device = Device(process)
        device.write_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                  ZCL_ATTR_DEVICE_CONFIG_TEXT, "C;D;")
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_REVISION) == "1"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_STATUS) == "1"
        process.exec("step_time 300")
        assert process.wait_for_exit(1.0)

    with StubProc() as process:
        device = Device(process)
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_TEXT) == "C;D;"


def test_config_nvm_failure_preserves_accepted_config_and_does_not_reboot() -> None:
    with StubProc(device_config="A;B;") as process:
        device = Device(process)
        assert process.exec("nvm_fail 1").ok
        result = process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} C;D;"
        )
        assert not result.ok
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_TEXT) == "A;B;"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_REVISION) == "0"
        process.exec("step_time 300")
        assert process.is_running()


def test_config_rejects_invalid_candidate_without_reboot() -> None:
    with StubProc(device_config="A;B;") as process:
        result = process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} invalid"
        )
        assert not result.ok
        process.exec("step_time 300")
        assert process.is_running()


def test_config_rejects_truncated_or_embedded_nul_raw_records_without_reboot() -> None:
    with StubProc(device_config="A;B;") as process:
        device = Device(process)
        prefix = (
            f"zcl_write_raw 1 {ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} "
        )

        embedded_nul = "04004100423b" + "00" * 124
        assert not process.exec(prefix + embedded_nul).ok
        assert not process.exec(prefix + "0400413b").ok
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_TEXT) == "A;B;"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_REVISION) == "0"
        process.exec("step_time 300")
        assert process.is_running()


def test_pending_config_rejects_every_candidate_without_replacing_state() -> None:
    with StubProc(device_config="A;B;") as process:
        device = Device(process)
        device.write_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                 ZCL_ATTR_DEVICE_CONFIG_TEXT, "C;D;")
        result = process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} invalid"
        )
        assert not result.ok
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_TEXT) == "C;D;"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_REVISION) == "1"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_STATUS) == "1"
        process.exec("step_time 300")
        assert process.wait_for_exit(1.0)


def test_pending_config_rejects_a_second_valid_write_without_replacing_state() -> None:
    with StubProc(device_config="A;B;") as process:
        device = Device(process)
        device.write_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                 ZCL_ATTR_DEVICE_CONFIG_TEXT, "C;D;")
        result = process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_TEXT:04X} E;F;"
        )
        assert not result.ok
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_TEXT) == "C;D;"
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_REVISION) == "1"
        process.exec("step_time 300")
        assert process.wait_for_exit(1.0)


def test_status_led_policy_is_explicit_for_present_and_absent_leds() -> None:
    with StubProc(device_config="A;B;SA0u;LB1;RB2;") as process:
        device = Device(process)
        device.write_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                 ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE, 1)
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE) == "1"


def test_status_led_mode_rejects_ledless_composition() -> None:
    with StubProc(device_config="A;B;") as process:
        result = process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_STATUS_LED_MODE:04X} 1"
        )
        assert not result.ok


def test_multi_press_reset_is_limited_by_gesture_capacity() -> None:
    with StubProc(device_config="A;B;") as process:
        device = Device(process)
        result = process.exec(
            f"zcl_write 1 0x{ZCL_CLUSTER_DEVICE_CONFIG:04X} "
            f"0x{ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET:04X} 11"
        )
        assert not result.ok
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_CONFIG,
                                       ZCL_ATTR_DEVICE_CONFIG_MULTI_PRESS_RESET) == "10"


def test_diagnostics_snapshot_updates_without_persistence(device: Device) -> None:
    device.step_time(42)
    assert int(device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_DIAGNOSTICS,
                                       ZCL_ATTR_DIAGNOSTICS_UPTIME_MS)) >= 42
