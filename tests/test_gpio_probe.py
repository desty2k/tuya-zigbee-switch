"""GPIO probe over the Device Diagnostics cluster, on the probe-only stub.

`stub_probe` is built with FEATURES="probe", the feature set of a probe
board. Pin electrical behaviour comes from the stub model
(src/stub/hal/gpio_probe.h): floating, externally driven and pulse-train pins.
"""

import struct

from client import StubProc
from conftest import Device
from management_protocol_consts import (
    DIAGNOSTICS_CAPABILITY_PROBE,
    GPIO_PROBE_CLASS_ACTIVE,
    GPIO_PROBE_CLASS_DRIVEN_HIGH,
    GPIO_PROBE_CLASS_DRIVEN_LOW,
    GPIO_PROBE_CLASS_FLOATING,
    GPIO_PROBE_PULL_UP,
    GPIO_PROBE_PULL_NONE,
    GPIO_PROBE_PULL_PHASE_MS,
    GPIO_PROBE_STATE_PULSE,
    GPIO_PROBE_STATUS_BAD_PIN,
    GPIO_PROBE_STATUS_DISABLED,
    GPIO_PROBE_STATUS_NOT_ARMED,
    GPIO_PROBE_STATUS_NOT_FLOATING,
    GPIO_PROBE_STATUS_OK,
    GPIO_PROBE_WATCH_OVER_RATE,
    ZCL_ATTR_DIAGNOSTICS_CAPABILITIES,
    ZCL_ATTR_DIAGNOSTICS_PROBE_CLASSES,
    ZCL_ATTR_DIAGNOSTICS_PROBE_LEVELS,
    ZCL_ATTR_DIAGNOSTICS_PROBE_PINS,
    ZCL_ATTR_DIAGNOSTICS_PROBE_STATE,
    ZCL_ATTR_DIAGNOSTICS_PROBE_STATUS,
    ZCL_ATTR_DIAGNOSTICS_PROBE_WATCH,
    ZCL_CLUSTER_DEVICE_DIAGNOSTICS,
    ZCL_CMD_DIAGNOSTICS_PROBE_ARM,
    ZCL_CMD_DIAGNOSTICS_PROBE_PULL_TEST,
    ZCL_CMD_DIAGNOSTICS_PROBE_PULSE,
    ZCL_CMD_DIAGNOSTICS_PROBE_SNAPSHOT,
    ZCL_CMD_DIAGNOSTICS_PROBE_STOP,
    ZCL_CMD_DIAGNOSTICS_PROBE_WATCH,
)
from zcl_consts import ZCL_ATTR_BASIC_MODEL_ID, ZCL_CLUSTER_BASIC

PROBE_CMD = ["./build/stub/stub_probe"]
PROBE_CONFIG = "_TZ3210_drzlxjne;TS011F-probe;"
TLSR8258_PROBE_PINS = [f"{port}{index}" for port in "ABCD" for index in range(8)
                       if (port, index) != ("A", 7)]


def pin_id(name: str) -> int:
    return (ord(name[0]) - ord("A")) << 8 | int(name[1:])


def pin_name(pin: int) -> str:
    return f"{chr(ord('A') + (pin >> 8))}{pin & 0xFF}"


class Probe:
    def __init__(self, device: Device) -> None:
        self.device = device
        self.p = device.p

    def command(self, cmd: int, payload: bytes = b"") -> str:
        return self.device.call_zigbee_cmd_raw(
            1, ZCL_CLUSTER_DEVICE_DIAGNOSTICS, cmd, payload)["result"]

    def attr(self, attr: int) -> str:
        return self.device.read_zigbee_attr(1, ZCL_CLUSTER_DEVICE_DIAGNOSTICS, attr)

    def octets(self, attr: int) -> bytes:
        return bytes.fromhex(self.attr(attr))

    def status(self) -> int:
        return int(self.attr(ZCL_ATTR_DIAGNOSTICS_PROBE_STATUS))

    def pins(self) -> list[str]:
        raw = self.octets(ZCL_ATTR_DIAGNOSTICS_PROBE_PINS)
        return [pin_name(pin) for (pin,) in struct.iter_unpack("<H", raw)]

    def classes(self) -> dict[str, int]:
        return dict(zip(self.pins(), self.octets(ZCL_ATTR_DIAGNOSTICS_PROBE_CLASSES)))

    def levels(self) -> dict[str, int]:
        bits = int(self.attr(ZCL_ATTR_DIAGNOSTICS_PROBE_LEVELS))
        return {pin: (bits >> i) & 1 for i, pin in enumerate(self.pins())}

    def watch(self) -> tuple[int, dict[str, tuple[int, int, int, int, int]]]:
        raw = self.octets(ZCL_ATTR_DIAGNOSTICS_PROBE_WATCH)
        (elapsed,) = struct.unpack_from("<I", raw)
        records = {}
        for pin, edges, min_us, max_us, level, flags in struct.iter_unpack(
                "<HIIIBB", raw[4:]):
            records[pin_name(pin)] = (edges, min_us, max_us, level, flags)
        return elapsed, records

    def model(self, pin: str, *args: str) -> None:
        assert self.p.exec(f"probe_pin {pin} {' '.join(args)}").ok

    def info(self, pin: str) -> dict[str, int]:
        res = self.p.exec(f"probe_pin_info {pin}")
        assert res.ok
        return {k: int(v) for k, v in res.payload.items() if k != "pin"}

    def step(self, ms: int) -> None:
        # One main-loop pass per millisecond, as on the device.
        for _ in range(ms):
            self.device.step_time(1)

    def pull_test(self, pins: list[str] | None = None) -> str:
        pins = pins or []
        payload = bytes([len(pins)]) + b"".join(struct.pack("<H", pin_id(p)) for p in pins)
        result = self.command(ZCL_CMD_DIAGNOSTICS_PROBE_PULL_TEST, payload)
        self.step(4 * GPIO_PROBE_PULL_PHASE_MS + 2)
        return result

    def arm(self, pin: str) -> str:
        return self.command(ZCL_CMD_DIAGNOSTICS_PROBE_ARM, struct.pack("<H", pin_id(pin)))

    def pulse(self, pin: str, level: int, ms: int) -> str:
        return self.command(ZCL_CMD_DIAGNOSTICS_PROBE_PULSE,
                            struct.pack("<HBH", pin_id(pin), level, ms))


def probe_session(config: str = PROBE_CONFIG) -> StubProc:
    return StubProc(cmd=PROBE_CMD, device_config=config)


def assert_untouched(probe: Probe, pins: list[str]) -> None:
    for pin in pins:
        info = probe.info(pin)
        assert (info["output"], info["pull"], info["contention"]) == (0, 0, 0), pin


def test_probe_board_boots_with_every_pin_untouched() -> None:
    with probe_session() as proc:
        probe = Probe(Device(proc))
        assert probe.device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MODEL_ID) == "TS011F-probe"
        caps = int(probe.attr(ZCL_ATTR_DIAGNOSTICS_CAPABILITIES))
        assert caps & DIAGNOSTICS_CAPABILITY_PROBE
        assert probe.pins() == TLSR8258_PROBE_PINS
        probe.step(50)
        for pin in TLSR8258_PROBE_PINS:
            info = probe.info(pin)
            assert (info["drives"], info["output"], info["pull"]) == (0, 0, 0), pin
        assert int(probe.attr(ZCL_ATTR_DIAGNOSTICS_PROBE_STATE)) == 0


def test_discovery_procedure_finds_meter_button_and_relay_pins() -> None:
    with probe_session() as proc:
        probe = Probe(Device(proc))
        probe.model("C2", "low")              # BL0937 CF, idle without load
        probe.model("C3", "train", "1000", "500")  # BL0937 CF1, 1 kHz
        probe.model("D2", "high")             # button with external pull-up

        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_SNAPSHOT) == "PROCESSED"
        levels = probe.levels()
        assert levels["D2"] == 1 and levels["C2"] == 0

        assert probe.pull_test() == "PROCESSED"
        classes = probe.classes()
        assert classes["A0"] == GPIO_PROBE_CLASS_FLOATING
        assert classes["C2"] == GPIO_PROBE_CLASS_DRIVEN_LOW
        assert classes["D2"] == GPIO_PROBE_CLASS_DRIVEN_HIGH
        assert classes["C3"] == GPIO_PROBE_CLASS_ACTIVE
        assert_untouched(probe, TLSR8258_PROBE_PINS)

        payload = struct.pack("<HB", 2, 2) + struct.pack(
            "<HBHB", pin_id("C3"), GPIO_PROBE_PULL_NONE, pin_id("D2"), GPIO_PROBE_PULL_UP)
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_WATCH, payload) == "PROCESSED"
        probe.step(300)
        probe.model("D2", "low")              # the user presses the button
        probe.step(150)
        probe.model("D2", "high")
        probe.step(1560)
        elapsed, records = probe.watch()
        assert elapsed == 2000
        cf1_edges, cf1_min, cf1_max, _, cf1_flags = records["C3"]
        assert cf1_edges == 2000 and cf1_min == cf1_max == 1000 and cf1_flags == 0
        assert records["D2"][0] == 1 and records["D2"][3] == 1
        assert_untouched(probe, ["C3", "D2"])

        # Relay candidate: arm, pulse, cutoff.
        assert probe.arm("A0") == "PROCESSED"
        assert probe.pulse("A0", 1, 100) == "PROCESSED"
        assert probe.info("A0")["output"] == 1
        assert probe.info("A0")["output_level"] == 1
        assert int(probe.attr(ZCL_ATTR_DIAGNOSTICS_PROBE_STATE)) & GPIO_PROBE_STATE_PULSE
        probe.step(100)
        assert_untouched(probe, ["A0"])

        # Each pulse needs a fresh arm; outputs are never driven.
        assert probe.pulse("A0", 1, 100) == "ACTION_DENIED"
        assert probe.status() == GPIO_PROBE_STATUS_NOT_ARMED
        assert probe.arm("C2") == "ACTION_DENIED"
        assert probe.status() == GPIO_PROBE_STATUS_NOT_FLOATING
        assert probe.arm("C3") == "ACTION_DENIED"
        for pin in ("C2", "C3", "D2"):
            assert probe.info(pin)["drives"] == 0


def test_rate_ceiling_flags_a_fast_pin() -> None:
    with probe_session() as proc:
        probe = Probe(Device(proc))
        probe.model("B4", "train", "40", "20")    # 25 kHz
        payload = struct.pack("<HB", 1, 1) + struct.pack("<HB", pin_id("B4"), 0)
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_WATCH, payload) == "PROCESSED"
        probe.step(20)
        _, records = probe.watch()
        assert records["B4"][4] == GPIO_PROBE_WATCH_OVER_RATE
        assert_untouched(probe, ["B4"])


def test_stop_command_ends_a_pulse_and_restores_the_pin() -> None:
    with probe_session() as proc:
        probe = Probe(Device(proc))
        probe.pull_test(["B5"])
        assert probe.arm("B5") == "PROCESSED"
        assert probe.pulse("B5", 0, 500) == "PROCESSED"
        assert probe.info("B5")["output"] == 1
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_STOP) == "PROCESSED"
        assert_untouched(probe, ["B5"])
        assert int(probe.attr(ZCL_ATTR_DIAGNOSTICS_PROBE_STATE)) == 0


def test_pins_of_configured_features_are_not_probeable() -> None:
    with probe_session("M;P;BC3u;LC4;") as proc:
        probe = Probe(Device(proc))
        pins = probe.pins()
        assert "C3" not in pins and "C4" not in pins and len(pins) == 29
        assert probe.arm("C3") == "INVALID_VALUE"
        assert probe.status() == GPIO_PROBE_STATUS_BAD_PIN


def test_recovery_graph_disables_the_probe() -> None:
    with probe_session("M;P;RC3;") as proc:
        probe = Probe(Device(proc))
        assert probe.device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MODEL_ID) == "recovery"
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_SNAPSHOT) == "ACTION_DENIED"
        assert probe.status() == GPIO_PROBE_STATUS_DISABLED
        assert probe.pins() == []


def test_malformed_and_unknown_probe_commands_are_rejected() -> None:
    with probe_session() as proc:
        probe = Probe(Device(proc))
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_ARM, b"\x00") == "MALFORMED_COMMAND"
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_PULSE, b"\x00\x00\x01") == \
            "MALFORMED_COMMAND"
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_PULL_TEST, b"\x02\x00\x00") == \
            "MALFORMED_COMMAND"
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_WATCH, b"\x01\x00\x01") == \
            "MALFORMED_COMMAND"
        assert probe.command(ZCL_CMD_DIAGNOSTICS_PROBE_STOP, b"\x00") == "MALFORMED_COMMAND"
        assert probe.command(0x40) == "SKIPPED"
        assert probe.status() == GPIO_PROBE_STATUS_OK


def test_regular_images_have_no_probe() -> None:
    with StubProc(device_config="A;B;") as proc:
        device = Device(proc)
        assert device.read_zigbee_attr(
            1, ZCL_CLUSTER_DEVICE_DIAGNOSTICS, ZCL_ATTR_DIAGNOSTICS_CAPABILITIES) == "1"
        res = proc.exec(f"zcl_read 1 0x{ZCL_CLUSTER_DEVICE_DIAGNOSTICS:04X} "
                        f"0x{ZCL_ATTR_DIAGNOSTICS_PROBE_STATUS:04X}")
        assert not res.ok
        assert device.call_zigbee_cmd_raw(
            1, ZCL_CLUSTER_DEVICE_DIAGNOSTICS, ZCL_CMD_DIAGNOSTICS_PROBE_ARM,
            b"\x00\x00")["result"] == "SKIPPED"
