#!/usr/bin/env python3
"""Generate and verify management-protocol surfaces from one JSON definition."""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "helper_scripts/management_protocol.json"
OUTPUTS = {
    "header": ROOT / "src/zigbee/management_protocol.h",
    "xml": ROOT / "src/silabs/custom-zcl.xml",
    "tests": ROOT / "tests/management_protocol_consts.py",
    "z2m": ROOT / "helper_scripts/templates/management_protocol.js.jinja",
    "zha": ROOT / "helper_scripts/templates/management_protocol.py.jinja",
}


def c_header(protocol):
    lines = ["#ifndef ZIGBEE_MANAGEMENT_PROTOCOL_H_", "#define ZIGBEE_MANAGEMENT_PROTOCOL_H_", "",
             "/* Generated from helper_scripts/management_protocol.json. */",
             f"#define {'ZIGBEE_MANAGEMENT_MANUFACTURER_CODE':<45} 0x{protocol['manufacturer_code']:04X}u", ""]
    for cluster in protocol["clusters"]:
        name = cluster["name"]
        lines.append(f"#define {'ZCL_CLUSTER_' + name:<45} 0x{cluster['id']:04X}u")
        prefix = cluster["attribute_prefix"]
        for attr_id, attr_name, _, _, _, _ in cluster["attributes"]:
            lines.append(f"#define {'ZCL_ATTR_' + prefix + '_' + attr_name:<45} 0x{attr_id:04X}u")
        lines.append("")
    lines.extend(["typedef enum {", "    DEVICE_CONFIG_STATUS_READY = 0,",
                  "    DEVICE_CONFIG_STATUS_REBOOT_PENDING,", "    DEVICE_CONFIG_STATUS_ERROR,",
                  "} device_config_status_t;", "", "typedef enum {",
                  "    DEVICE_CONFIG_ERROR_NONE = 0,", "    DEVICE_CONFIG_ERROR_ARGUMENT,",
                  "    DEVICE_CONFIG_ERROR_PARSE,", "    DEVICE_CONFIG_ERROR_RESOURCE,",
                  "    DEVICE_CONFIG_ERROR_NVM,", "    DEVICE_CONFIG_ERROR_PENDING,",
                  "    DEVICE_CONFIG_ERROR_UNSUPPORTED,", "    DEVICE_CONFIG_ERROR_OVERFLOW,",
                  "} device_config_error_t;", "", "typedef enum {",
                  "    DEVICE_CONFIG_LED_NETWORK = 0,", "    DEVICE_CONFIG_LED_OFF,",
                  "    DEVICE_CONFIG_LED_ON,", "} device_config_led_mode_t;", "",
                  "#define DIAGNOSTICS_CAPABILITY_CORE    (1u << 0)", "", "#endif"])
    return "\n".join(lines) + "\n"


def xml(protocol):
    lines = ['<?xml version="1.0"?>', '<configurator>']
    for cluster in protocol["clusters"]:
        lines.append(f'  <clusterExtension code="0x{cluster["id"]:04x}" manufacturerCode="0x{protocol["manufacturer_code"]:04x}">')
        for attr_id, name, xml_type, _, access, default in cluster["attributes"]:
            writable = "true" if access == "rw" else "false"
            length = ' length="128"' if xml_type == "LONG_CHAR_STRING" else ""
            default_text = default if isinstance(default, str) else str(default)
            lines.append(f'    <attribute side="server" code="0x{attr_id:04x}" define="{name}" type="{xml_type}"{length} writable="{writable}" default="{default_text}">{name.lower().replace("_", " ")}</attribute>')
        lines.append("  </clusterExtension>")
    return "\n".join(lines + ["</configurator>", ""])


def test_constants(protocol):
    lines = ["# Generated from helper_scripts/management_protocol.json.",
             f"ZIGBEE_MANAGEMENT_MANUFACTURER_CODE = 0x{protocol['manufacturer_code']:04X}", ""]
    for cluster in protocol["clusters"]:
        name = cluster["name"]
        lines.append(f"ZCL_CLUSTER_{name} = 0x{cluster['id']:04X}")
        prefix = cluster["attribute_prefix"]
        for attr_id, attr_name, _, _, _, _ in cluster["attributes"]:
            lines.append(f"ZCL_ATTR_{prefix}_{attr_name} = 0x{attr_id:04X}")
        lines.append("")
    return "\n".join(lines)


def z2m_template(protocol):
    return "// Generated from helper_scripts/management_protocol.json.\n" + \
        f"const managementProtocol = {{manufacturerCode: 0x{protocol['manufacturer_code']:04X}, deviceConfig: 0x{protocol['clusters'][0]['id']:04X}, deviceDiagnostics: 0x{protocol['clusters'][1]['id']:04X}}};\n"


def zha_template(protocol):
    return "# Generated from helper_scripts/management_protocol.json.\n" + \
        f"MANAGEMENT_PROTOCOL = {{'manufacturer_code': 0x{protocol['manufacturer_code']:04X}, 'device_config': 0x{protocol['clusters'][0]['id']:04X}, 'device_diagnostics': 0x{protocol['clusters'][1]['id']:04X}}}\n"


def render(protocol):
    return {"header": c_header(protocol), "xml": xml(protocol),
            "tests": test_constants(protocol), "z2m": z2m_template(protocol),
            "zha": zha_template(protocol)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = render(json.loads(SOURCE.read_text()))
    stale = [name for name, path in OUTPUTS.items() if not path.exists() or path.read_text() != rendered[name]]
    if args.check:
        if stale:
            raise SystemExit("management protocol outputs are stale: " + ", ".join(stale))
        return
    for name, path in OUTPUTS.items():
        path.write_text(rendered[name])


if __name__ == "__main__":
    main()
