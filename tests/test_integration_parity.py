"""The generated Zigbee2MQTT, ZHA and HOMEd integrations against the firmware.

Every attribute an integration reads, writes or configures must exist in the
stub's registered descriptor on the same cluster, with the same ZCL data type,
writable when the integration writes it, and under the manufacturer code the
firmware registered the cluster with (Telink rejects a mismatch).
"""

import json
import re
import subprocess
import sys
from pathlib import Path

import pytest

from client import StubProc

ROOT = Path(__file__).resolve().parent.parent
PROTOCOL = json.loads((ROOT / "helper_scripts/management_protocol.json").read_text())
HAL_ATTR_WRITE = 1 << 1

# Endpoints: switch 1, relay 2 (with indicator), cover switch 3, cover 4.
FULL_CONFIG = "M;Parity;SA2u;RB2;IC5;XA0A1u;CB0B1;BTA5;"
PROBE_CMD = ["./build/stub/stub_probe"]
PROBE_CONFIG = "_TZ3210_drzlxjne;TS011F-probe;"

Z2M_TYPES = {"BOOLEAN": 0x10, "BITMAP8": 0x18, "BITMAP32": 0x1B, "UINT8": 0x20, "UINT16": 0x21,
             "UINT32": 0x23, "ENUM8": 0x30, "OCTET_STR": 0x41, "LONG_CHAR_STR": 0x44}
Z2M_CLUSTERS = {"genBasic": 0x0000, "genPowerCfg": 0x0001, "genOnOff": 0x0006,
                "genOnOffSwitchCfg": 0x0007, "genMultistateInput": 0x0012,
                "closuresWindowCovering": 0x0102}
ZIGPY_TYPES = {"t.Bool": 0x10, "t.bitmap8": 0x18, "t.bitmap32": 0x1B, "t.uint8_t": 0x20,
               "t.uint16_t": 0x21, "t.uint32_t": 0x23, "t.enum8": 0x30, "t.LVBytes": 0x41,
               "t.LongCharacterString": 0x44}
ZIGPY_CLUSTERS = {"Basic": 0x0000, "OnOff": 0x0006, "OnOffConfiguration": 0x0007,
                  "MultistateInput": 0x0012, "WindowCovering": 0x0102}

# (cluster, attribute, data type, written, manufacturer code)
Reference = tuple[int, int, int, bool, int]


def firmware_descriptor(cmd: list[str] | None, config: str) -> dict[int, tuple[int, dict]]:
    """Server clusters of every endpoint: id -> (manufacturer, {attr: (type, flags)})."""
    proc = StubProc(cmd=cmd, device_config=config) if cmd else StubProc(device_config=config)
    descriptor: dict[int, tuple[int, dict]] = {}
    with proc:
        res = proc.exec("zcl_clusters")
        assert res.ok, res.payload
        for entry in res.payload["clusters"].split(","):
            endpoint, cluster, role, _ = entry.split(":")
            if role != "S":
                continue
            res = proc.exec(f"zcl_attrs {endpoint} {cluster[2:]}")
            assert res.ok, res.payload
            attrs = descriptor.setdefault(int(cluster, 16), (int(res.payload["manufacturer"], 16), {}))[1]
            for attr in filter(None, res.payload["attrs"].split(",")):
                attr_id, data_type, flags = attr.split(":")
                attrs[int(attr_id, 16)] = (int(data_type, 16), int(flags, 16))
    return descriptor


@pytest.fixture(scope="module")
def firmware() -> dict[int, tuple[int, dict]]:
    descriptor = firmware_descriptor(None, FULL_CONFIG)
    probe = firmware_descriptor(PROBE_CMD, PROBE_CONFIG)
    diagnostics = PROTOCOL["clusters"][1]["id"]
    descriptor[diagnostics][1].update(probe[diagnostics][1])
    return descriptor


def generate(script: str, *args: str) -> str:
    return subprocess.run([sys.executable, f"helper_scripts/{script}", *args, "device_db.yaml"],
                          cwd=ROOT, check=True, capture_output=True, text=True).stdout


def z2m_attribute_lines(text: str) -> list[tuple[str, int, int, bool]]:
    return [(name, int(attr_id, 16), Z2M_TYPES[kind], bool(write)) for name, attr_id, kind, write in
            re.findall(r"^\s+(\w+): \{ID: (0x[0-9A-Fa-f]{4}), type: Zcl\.DataType\.(\w+)(, write: true)?\},$",
                       text, re.MULTILINE)]


def z2m_references(text: str) -> set[Reference]:
    fragment = text[text.index("const managementProtocol = {"):]
    fragment = fragment[:fragment.index("\n};\n")]
    code = int(re.search(r"^    manufacturerCode: (0x[0-9A-F]+),$", fragment, re.MULTILINE)[1], 16)
    protocol = {}
    for section in re.split(r"^    (?=\w+: \{$)", fragment, flags=re.MULTILINE)[1:]:
        name = section.split(":")[0]
        cluster_id = re.search(r"^        ID: (0x[0-9A-F]+),$", section, re.MULTILINE)
        protocol[name] = (int(cluster_id[1], 16) if cluster_id else None, z2m_attribute_lines(section))
    # Probe attributes go on Device Diagnostics.
    protocol["probe"] = (protocol["deviceDiagnostics"][0], protocol["probe"][1])

    refs: set[Reference] = set()
    for block in re.findall(r"deviceAddCustomCluster\(\"\w+\", \{\n(.*?)\n\s+\}\),", text, re.DOTALL):
        cluster_expr = re.search(r"^\s+ID: ([\w.]+),$", block, re.MULTILINE)[1]
        cluster = (protocol[cluster_expr.split(".")[1]][0] if cluster_expr.startswith("managementProtocol")
                   else int(cluster_expr, 16))
        code_expr = re.search(r"^\s+manufacturerCode: ([\w.]+),$", block, re.MULTILINE)
        manufacturer = 0 if code_expr is None else (
            code if code_expr[1] == "managementProtocol.manufacturerCode" else int(code_expr[1], 16))
        attributes = z2m_attribute_lines(block)
        for source in re.findall(r"managementProtocol\.(\w+)\.attributes", block):
            attributes += protocol[source][1]
        refs |= {(cluster, attr_id, kind, write, manufacturer) for _, attr_id, kind, write in attributes}
    # Standard-cluster attributes addressed inline by id.
    for cluster, attr_id, kind in re.findall(
            r"cluster: \"(\w+)\",\s*attribute: \{ ?ID: (0x[0-9A-Fa-f]{4}), type: (0x[0-9A-Fa-f]+|Zcl\.DataType\.\w+) ?\}",
            text):
        kind = int(kind, 16) if kind.startswith("0x") else Z2M_TYPES[kind.split(".")[-1]]
        refs.add((Z2M_CLUSTERS[cluster], int(attr_id, 16), kind, False, 0))
    return refs


def zha_references(text: str) -> set[Reference]:
    code = PROTOCOL["manufacturer_code"]
    enums = set(re.findall(r"^class (\w+)\(t\.enum8\):", text, re.MULTILINE))
    names = {"MANAGEMENT_PROTOCOL[\"manufacturer_code\"]": code}
    names |= {f"MANAGEMENT_PROTOCOL[\"{key}\"]": value
              for key, value in eval(re.search(r"^MANAGEMENT_PROTOCOL = (\{.*\})$", text, re.MULTILINE)[1]).items()}
    tables = {name: re.findall(r"\((0x[0-9A-F]{4}), \"\w+\", ([\w.]+), \"(\w+)\"\)", body)
              for name, body in re.findall(r"^(\w+_ATTRIBUTES) = \(\n(.*?)\n\)", text, re.MULTILINE | re.DOTALL)}

    def data_type(kind: str) -> int:
        return 0x30 if kind in enums else ZIGPY_TYPES[kind]

    refs: set[Reference] = set()
    for base, body in re.findall(r"^class \w+\(CustomCluster(?:, (\w+))?\):\n(.*?)(?=^\S)", text,
                                 re.MULTILINE | re.DOTALL):
        cluster_expr = re.search(r"^    cluster_id = (.+)$", body, re.MULTILINE)
        cluster = ZIGPY_CLUSTERS[base] if base else (
            names[cluster_expr[1]] if cluster_expr[1] in names else int(cluster_expr[1], 16))
        override = re.search(r"^    manufacturer_id_override = (.+)$", body, re.MULTILINE)
        override = names[override[1]] if override else None
        attributes = [(int(a, 16), data_type(k), acc, "True") for table in
                      re.findall(r"management_attribute_defs\((\w+)\)", body) for a, k, acc in tables[table]]
        for args in re.findall(r"ZCLAttributeDef\((.*?)\)", body, re.DOTALL):
            specific = re.search(r"is_manufacturer_specific=(\w+)", args)
            attributes.append((int(re.search(r"id=(0x[0-9a-fA-F]+)", args)[1], 16),
                               data_type(re.search(r"type=([\w.]+)", args)[1]),
                               re.search(r"access=\"([\w*]+)\"", args)[1],
                               specific[1] if specific else "None"))
        for attr_id, kind, access, specific in attributes:
            # zigpy sends a code for manufacturer-specific attributes, and for any
            # attribute of a 0xFC00+ cluster unless told otherwise; without an
            # override that is the device's (stock Tuya) code.
            uses_code = specific == "True" or (specific == "None" and cluster >= 0xFC00)
            manufacturer = (override if override is not None else -1) if uses_code else 0
            refs.add((cluster, attr_id, kind, "w" in access, manufacturer))
    return refs


def homed_references(text: str) -> set[Reference]:
    refs: set[Reference] = set()
    for entries in json.loads(text).values():
        for entry in entries:
            for attr in entry.get("options", {}).get("customAttributes", {}).values():
                refs.add((attr["clusterId"], attr["attributeId"], attr["dataType"],
                          bool(attr.get("action")), attr.get("manufacturerCode", 0)))
    return refs


def mismatches(refs: set[Reference], firmware: dict[int, tuple[int, dict]]) -> list[str]:
    errors = []
    for cluster, attr_id, kind, written, manufacturer in sorted(refs):
        where = f"cluster 0x{cluster:04X} attribute 0x{attr_id:04X}"
        if cluster not in firmware:
            errors.append(f"{where}: cluster not registered")
            continue
        code, attrs = firmware[cluster]
        if attr_id not in attrs:
            errors.append(f"{where}: not registered")
            continue
        fw_type, flags = attrs[attr_id]
        if kind != fw_type:
            errors.append(f"{where}: type 0x{kind:02X}, firmware 0x{fw_type:02X}")
        if written and not flags & HAL_ATTR_WRITE:
            errors.append(f"{where}: written, firmware read-only")
        if manufacturer != code:
            errors.append(f"{where}: manufacturer code {manufacturer:#06x}, firmware {code:#06x}")
    return errors


@pytest.mark.parametrize("script,args,parse", [
    ("make_z2m_custom_converters.py", (), z2m_references),
    ("make_z2m_custom_converters.py", ("--z2m-v1",), z2m_references),
    ("make_zha_quirk.py", (), zha_references),
    ("make_homed_extension.py", (), homed_references),
])
def test_integration_attributes_match_the_firmware(firmware, script, args, parse) -> None:
    refs = parse(generate(script, *args))
    management = {cluster["id"] for cluster in PROTOCOL["clusters"]}
    assert {ref[0] for ref in refs} >= management | {0xFC01, 0xFC02, 0x0102}
    assert mismatches(refs, firmware) == []


def test_management_protocol_matches_the_firmware(firmware) -> None:
    for cluster in PROTOCOL["clusters"]:
        code, attrs = firmware[cluster["id"]]
        assert code == PROTOCOL["manufacturer_code"]
        expected = cluster["attributes"] + cluster.get("probe", {}).get("attributes", [])
        assert {attr_id: (Z2M_TYPES[kind], access == "rw") for attr_id, _, _, kind, access, _ in expected} == \
            {attr_id: (kind, bool(flags & HAL_ATTR_WRITE)) for attr_id, (kind, flags) in attrs.items()}
    for cluster in (0xFC01, 0xFC02):
        assert firmware[cluster][0] == PROTOCOL["manufacturer_code"]


def test_parity_check_reports_a_mismatch(firmware) -> None:
    text = generate("make_z2m_custom_converters.py")
    assert mismatches(z2m_references(text), firmware) == []
    wrong_type = text.replace("overrun: {ID: 0xff06, type: Zcl.DataType.UINT16",
                              "overrun: {ID: 0xff06, type: Zcl.DataType.UINT32")
    assert any("0xFF06: type" in e for e in mismatches(z2m_references(wrong_type), firmware))
    wrong_code = text.replace("    manufacturerCode: 0x1A2B,", "    manufacturerCode: 0x125D,", 1)
    assert any("manufacturer code" in e for e in mismatches(z2m_references(wrong_code), firmware))


def test_probe_board_has_a_z2m_converter_matching_the_discovery_helper() -> None:
    """The probe board is `build: no` but the discovery helper drives it through
    Zigbee2MQTT; ZHA and HOMEd have no probe support."""
    sys.path.insert(0, str(ROOT / "helper_scripts"))
    import probe_discovery

    text = generate("make_z2m_custom_converters.py")
    assert 'zigbeeModel: ["TS011F-probe"]' in text
    assert "romasku.probe()," in text
    fragment = text[text.index("    probe: {\n        capabilityBit:"):]
    names = [name for name, *_ in z2m_attribute_lines(fragment[:fragment.index("        commands:")])]
    published = {re.sub(r"[A-Z]", lambda m: "_" + m[0].lower(), name).removeprefix("probe_") for name in names}
    assert published == set(probe_discovery.ATTRIBUTES)
    commands = dict(re.findall(r"^\s+(\w+): \{name: \"\w+\", ID: (0x[0-9A-F]{2})", fragment, re.MULTILINE))
    assert {int(cmd_id, 16) for cmd_id in commands.values()} == set(probe_discovery.COMMANDS.values())
    assert "TS011F-probe" not in generate("make_zha_quirk.py")
    assert "TS011F-probe" not in generate("make_homed_extension.py")
