#!/usr/bin/env python3
"""Find the pins of a device running the GPIO probe image, through Zigbee2MQTT.

The probe image (a device_db board whose `features` include `probe`) answers
probe commands on the Device Diagnostics cluster; the protocol is defined in
helper_scripts/management_protocol.json. The procedure is in
docs/contribute/porting.md, section "GPIO probe":

  snapshot -> pull test -> watch with no load -> watch while the button is
  pressed -> watch with a resistive load -> pulse each floating pin, and each
  quiet driven-low pin briefly high, with the load unplugged, asking what was
  heard or seen.

It prints a proposed config string and meter pin map. Nothing is stored on
the device; a reboot ends every probe operation. Pins claimed by the device
config are not probeable.

Zigbee2MQTT needs the fork's converter for the probe board, which declares the
cluster's commands and publishes the probe attributes under the `probe` key
of the device state as raw values (octet strings as hex). Requires
paho-mqtt (`requirements.txt`).
"""

import argparse
import json
import struct
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

PROTOCOL = json.loads((Path(__file__).resolve().parent /
                       "management_protocol.json").read_text())
CLUSTER = next(c for c in PROTOCOL["clusters"] if "probe" in c)
PROBE = CLUSTER["probe"]
STATUS = PROBE["status"]
CLASSES = PROBE["classes"]
LIMITS = PROBE["limits"]
COMMANDS = {name: cmd_id for cmd_id, name, _ in PROBE["commands"]}
ATTRIBUTES = {name.removeprefix("PROBE_").lower(): attr_id
              for attr_id, name, *_ in PROBE["attributes"]}
PULL = {name: value for value, name in enumerate(PROBE["pulls"])}
OVER_RATE = 1 << PROBE["watch_flags"].index("OVER_RATE")
WATCH_CAPACITY = 2          # TLSR8258 counters (src/telink/hal/gpio_probe.c)


def pin_name(pin: int) -> str:
    return f"{chr(ord('A') + (pin >> 8))}{pin & 0xFF}"


def pin_id(name: str) -> int:
    return (ord(name[0].upper()) - ord("A")) << 8 | int(name[1:])


def parse_pins(raw: bytes) -> list[str]:
    return [pin_name(pin) for (pin,) in struct.iter_unpack("<H", raw)]


def parse_classes(pins: list[str], raw: bytes) -> dict[str, str]:
    return {pin: CLASSES[value] for pin, value in zip(pins, raw)}


def parse_levels(pins: list[str], bits: int) -> dict[str, int]:
    return {pin: (bits >> i) & 1 for i, pin in enumerate(pins)}


@dataclass
class WatchRecord:
    edges: int
    min_interval_us: int
    max_interval_us: int
    level: int
    over_rate: bool


def parse_watch(raw: bytes) -> tuple[int, dict[str, WatchRecord]]:
    (elapsed_ms,) = struct.unpack_from("<I", raw)
    records = {}
    for pin, edges, min_us, max_us, level, flags in struct.iter_unpack("<HIIIBB", raw[4:]):
        records[pin_name(pin)] = WatchRecord(edges, min_us, max_us, level,
                                             bool(flags & OVER_RATE))
    return elapsed_ms, records


def rate_hz(record: WatchRecord, elapsed_ms: int) -> float:
    return record.edges * 1000.0 / elapsed_ms if elapsed_ms else 0.0


@dataclass
class Findings:
    classes: dict[str, str] = field(default_factory=dict)
    idle_hz: dict[str, float] = field(default_factory=dict)
    loaded_hz: dict[str, float] = field(default_factory=dict)
    button_edges: dict[str, int] = field(default_factory=dict)
    relay: list[str] = field(default_factory=list)
    leds: dict[str, int] = field(default_factory=dict)   # pin -> level that lights it
    sel: list[str] = field(default_factory=list)


def propose(findings: Findings, manufacturer: str, model: str) -> tuple[str, dict]:
    """Config string and meter map from the procedure's observations."""
    tokens = []
    for pin, on_level in sorted(findings.leds.items()):
        tokens.append(f"L{pin}" + ("" if on_level else "i"))
    buttons = sorted(pin for pin, edges in findings.button_edges.items() if edges > 0)
    for pin in buttons[:1]:
        tokens.append(f"S{pin}u")
    for pin in findings.relay[:1]:
        tokens.append(f"R{pin}")
    config = ";".join([manufacturer, model, *tokens, "M"]) + ";"

    pulsing = {pin for pin, hz in findings.idle_hz.items() if hz > 0}
    rising = sorted((pin for pin, hz in findings.loaded_hz.items()
                     if hz > findings.idle_hz.get(pin, 0.0) * 1.5 and hz > 0),
                    key=lambda pin: -findings.loaded_hz[pin])
    meter: dict = {}
    cf1 = sorted(pulsing - set(rising))
    if rising or cf1:
        meter["ic"] = "BL0937"
        meter["cf"] = rising[0] if rising else None
        meter["cf1"] = cf1[0] if cf1 else None
        meter["sel"] = findings.sel[0] if findings.sel else None
    else:
        meter["ic"] = "no pulse output seen: BL0942 (UART) suspected"
    return config, meter


class Z2MProbe:
    """Probe commands and attribute reads through the Zigbee2MQTT MQTT API."""

    def __init__(self, args: argparse.Namespace) -> None:
        import paho.mqtt.client as mqtt

        self.topic = f"{args.base_topic}/{args.device}"
        self.state: dict = {}
        self.updated = threading.Event()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if args.user:
            self.client.username_pw_set(args.user, args.password)
        self.client.on_message = self._on_message
        self.client.connect(args.host, args.port)
        self.client.subscribe(self.topic)
        self.client.loop_start()

    def _on_message(self, client, userdata, message) -> None:
        payload = json.loads(message.payload or b"{}")
        if "probe" in payload:
            self.state.update(payload["probe"])
            self.updated.set()

    def _publish(self, body: dict) -> None:
        self.client.publish(f"{self.topic}/set", json.dumps(body))

    def command(self, name: str, payload: dict | None = None) -> str:
        self._publish({"command": {
            "cluster": CLUSTER["id"], "command": COMMANDS[name],
            "payload": payload or {},
            "options": {"manufacturerCode": PROTOCOL["manufacturer_code"]}}})
        time.sleep(0.5)
        return STATUS[self.read("status")]

    def read(self, name: str):
        """One probe attribute, by its lowercase name without `PROBE_`.

        Each attribute is read in its own Read Attributes request: the pins,
        classes and watch octet strings together exceed one unfragmented APS
        payload, and the stack does not fragment the response.
        """
        self.updated.clear()
        self.state.pop(name, None)
        self._publish({"read": {"cluster": CLUSTER["id"], "attributes": [ATTRIBUTES[name]],
                                "options": {"manufacturerCode": PROTOCOL["manufacturer_code"]}}})
        deadline = time.monotonic() + 10
        while name not in self.state:
            if not self.updated.wait(max(0.0, deadline - time.monotonic())):
                raise SystemExit(f"no probe {name} from Zigbee2MQTT; "
                                 "is the probe converter installed?")
            self.updated.clear()
        return self.state[name]

    def pins(self) -> list[str]:
        return parse_pins(bytes.fromhex(self.read("pins")))

    def watch(self, pins: list[tuple[str, str]], seconds: int) -> tuple[int, dict]:
        raw = b"".join(struct.pack("<HB", pin_id(pin), PULL[pull]) for pin, pull in pins)
        status = self.command("PROBE_WATCH", {"duration_s": seconds, "count": len(pins),
                                              "pins": list(raw)})
        if status != "OK":
            raise SystemExit(f"watch {pins}: {status}")
        time.sleep(seconds + 1)
        return parse_watch(bytes.fromhex(self.read("watch")))

    def pulse(self, pin: str, level: int, ms: int) -> str:
        status = self.command("PROBE_ARM", {"pin": pin_id(pin)})
        if status != "OK":
            return status
        return self.command("PROBE_PULSE", {"pin": pin_id(pin), "level": level,
                                            "duration_ms": ms})


def batches(items: list, size: int) -> list[list]:
    return [items[i:i + size] for i in range(0, len(items), size)]


def ask(question: str) -> str:
    return input(f"{question} ").strip().lower()


def watch_all(probe: Z2MProbe, pins: list[str], pull: str, seconds: int,
              prompt: str | None = None) -> tuple[dict, dict]:
    rates, edges = {}, {}
    for batch in batches(pins, WATCH_CAPACITY):
        if prompt:
            print(f"  {prompt} ({seconds} s, pins {', '.join(batch)})")
        elapsed, records = probe.watch([(pin, pull) for pin in batch], seconds)
        for pin, record in records.items():
            rates[pin] = float("inf") if record.over_rate else rate_hz(record, elapsed)
            edges[pin] = record.edges
            print(f"    {pin}: {record.edges} edges, {rates[pin]:.1f} Hz, "
                  f"interval {record.min_interval_us}-{record.max_interval_us} us"
                  f"{' OVER RATE' if record.over_rate else ''}")
    return rates, edges


def run(args: argparse.Namespace) -> None:
    probe = Z2MProbe(args)
    try:
        discover(probe, args)
    finally:
        probe.command("PROBE_STOP")


def discover(probe: Z2MProbe, args: argparse.Namespace) -> None:
    found = Findings()
    print("Before converting another outlet, record its stock identity from "
          "Zigbee2MQTT (docs/contribute/porting.md).")
    if ask("Nothing plugged into the outlet? [y/N]") != "y":
        raise SystemExit("unplug the load first")

    pins = probe.pins()
    print(f"Probeable pins: {' '.join(pins)}")
    print(f"snapshot: {probe.command('PROBE_SNAPSHOT')}")
    levels = parse_levels(pins, probe.read("levels"))
    print("  " + " ".join(f"{pin}={level}" for pin, level in levels.items()))

    print(f"pull test: {probe.command('PROBE_PULL_TEST', {'count': 0, 'pins': []})}")
    time.sleep(LIMITS["PULL_PHASE_MS"] * 4 / 1000 + 0.5)
    found.classes = parse_classes(pins, bytes.fromhex(probe.read("classes")))
    for name in CLASSES[1:]:
        print(f"  {name}: {' '.join(p for p, c in found.classes.items() if c == name)}")

    driven = [p for p, c in found.classes.items() if c != "FLOATING"]
    floating = [p for p, c in found.classes.items() if c == "FLOATING"]
    print("Watch with no load (BL0937 CF1 pulses from mains voltage):")
    found.idle_hz, _ = watch_all(probe, driven, "NONE", args.watch_s)
    print("Watch while you press the button repeatedly:")
    _, found.button_edges = watch_all(probe, [p for p in driven if levels.get(p)] + floating,
                                      "UP", args.watch_s, "press the button now")
    if ask("Plug in a resistive load (kettle, heater; not a motor) and switch it on "
           "if the outlet passes power. Ready? [y/N]") == "y":
        found.loaded_hz, _ = watch_all(probe, driven, "NONE", args.watch_s)
        ask("Unplug the load now, then press Enter.")

    cf1 = [p for p, hz in found.idle_hz.items() if 0 < hz < float("inf")]
    for pin in floating:
        for level in (1, 0):
            status = probe.pulse(pin, level, LIMITS["PULSE_MAX_MS"] if cf1 else 200)
            if status != "OK":
                print(f"  {pin} level {level}: {status}")
                continue
            answer = ask(f"  {pin} driven {level}: [c]lick, [l]ED lit, [n]othing?")
            if answer.startswith("c") and pin not in found.relay:
                found.relay.append(pin)
            elif answer.startswith("l"):
                found.leds[pin] = level
        if cf1:
            baseline = found.idle_hz[cf1[0]]
            probe.command("PROBE_WATCH", {"duration_s": 1, "count": 1,
                                          "pins": list(struct.pack("<HB", pin_id(cf1[0]), 0))})
            probe.pulse(pin, 1, LIMITS["PULSE_MAX_MS"])
            time.sleep(1.5)
            elapsed, records = parse_watch(bytes.fromhex(probe.read("watch")))
            hz = rate_hz(records[cf1[0]], elapsed)
            if baseline and abs(hz - baseline) / baseline > 0.2:
                print(f"  {cf1[0]} changed {baseline:.1f} -> {hz:.1f} Hz while {pin} was high: SEL")
                found.sel.append(pin)

    # A driven-low pin is a low-resistance relay transistor base or a chip
    # output; pins that pulsed in a watch are meter outputs and stay untouched.
    quiet_low = [p for p, c in found.classes.items() if c == "DRIVEN_LOW"
                 and not found.idle_hz.get(p) and not found.loaded_hz.get(p)]
    for pin in quiet_low:
        status = probe.pulse(pin, 1, LIMITS["DRIVEN_LOW_PULSE_MAX_MS"])
        if status != "OK":
            print(f"  {pin} level 1: {status}")
            continue
        if ask(f"  {pin} (driven low) lifted high: [c]lick, [n]othing?").startswith("c") \
                and pin not in found.relay:
            found.relay.append(pin)

    config, meter = propose(found, args.manufacturer, args.model)
    print(f"\nProposed config string: {config}")
    print(f"Meter pin map: {json.dumps(meter)}")
    print("Record the results in docs/refactor/capabilities/STATE.md.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("device", help="Zigbee2MQTT friendly name of the probe device")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--user")
    parser.add_argument("--password")
    parser.add_argument("--base-topic", default="zigbee2mqtt")
    parser.add_argument("--watch-s", type=int, default=8)
    parser.add_argument("--manufacturer", default="drzlxjne")
    parser.add_argument("--model", default="TS011F-BS-PM")
    try:
        run(parser.parse_args())
    except KeyboardInterrupt:
        sys.exit("interrupted; the probe ends every operation by itself or on reboot")


if __name__ == "__main__":
    main()
