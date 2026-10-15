#!/usr/bin/env python3
"""Generate and verify management-protocol surfaces from one JSON definition.

A cluster's optional `probe` block is the GPIO probe interface, compiled only
into images with the `probe` feature (Telink). It goes into the C header, the
probe codes header and the test constants, not into the Silabs XML.
"""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "helper_scripts/management_protocol.json"
OUTPUTS = {
    "header": ROOT / "src/zigbee/management_protocol.h",
    "xml": ROOT / "src/silabs/custom-zcl.xml",
    "probe": ROOT / "src/base_components/gpio_probe_codes.h",
    "tests": ROOT / "tests/management_protocol_consts.py",
    "z2m": ROOT / "helper_scripts/templates/management_protocol.js.jinja",
    "zha": ROOT / "helper_scripts/templates/management_protocol.py.jinja",
}


# C type and constant prefix of each DEVICE_CONFIG enum.
C_ENUMS = {
    "STATUS": ("device_config_status_t", "DEVICE_CONFIG_STATUS"),
    "LAST_ERROR": ("device_config_error_t", "DEVICE_CONFIG_ERROR"),
    "STATUS_LED_MODE": ("device_config_led_mode_t", "DEVICE_CONFIG_LED"),
}

# ZCL data type ids, by the herdsman `Zcl.DataType` names used in the JSON.
ZCL_TYPE_IDS = {"BOOLEAN": 0x10, "BITMAP8": 0x18, "BITMAP32": 0x1B, "UINT8": 0x20, "UINT16": 0x21,
                "UINT32": 0x23, "ENUM8": 0x30, "OCTET_STR": 0x41, "LONG_CHAR_STR": 0x44}

# zigpy types for the same names.
ZIGPY_TYPES = {"BOOLEAN": "t.Bool", "BITMAP8": "t.bitmap8", "BITMAP32": "t.bitmap32", "UINT8": "t.uint8_t",
               "UINT16": "t.uint16_t", "UINT32": "t.uint32_t", "ENUM8": "t.enum8",
               "OCTET_STR": "t.LVBytes", "LONG_CHAR_STR": "t.LongCharacterString"}

# herdsman parameter types of probe command fields.
Z2M_PARAMETER_TYPES = {"uint8": "Zcl.DataType.UINT8", "uint16": "Zcl.DataType.UINT16",
                       "uint16[count]": "Zcl.BuffaloZclDataType.LIST_UINT16"}


def config_cluster(protocol):
    return next(cluster for cluster in protocol["clusters"] if "enums" in cluster)


def camel(name):
    head, *rest = name.lower().split("_")
    return head + "".join(part.capitalize() for part in rest)


def probe_block(protocol):
    for cluster in protocol["clusters"]:
        if "probe" in cluster:
            return cluster, cluster["probe"]
    raise SystemExit("management protocol has no probe block")


def record_size(fields):
    sizes = {"uint8": 1, "uint16": 2, "uint32": 4}
    return sum(sizes[kind] for _, kind in fields)


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
        if "probe" in cluster:
            for attr_id, attr_name, _, _, _, _ in cluster["probe"]["attributes"]:
                lines.append(f"#define {'ZCL_ATTR_' + prefix + '_' + attr_name:<45} 0x{attr_id:04X}u")
            for cmd_id, cmd_name, _ in cluster["probe"]["commands"]:
                lines.append(f"#define {'ZCL_CMD_' + prefix + '_' + cmd_name:<45} 0x{cmd_id:02X}u")
        lines.append("")
    enums = config_cluster(protocol)["enums"]
    for key, (type_name, prefix) in C_ENUMS.items():
        lines += c_enum(type_name, prefix, enums[key])
    lines.extend([*aligned_defines([
                      ("DIAGNOSTICS_CAPABILITY_CORE", "(1u << 0)"),
                      ("DIAGNOSTICS_CAPABILITY_PROBE",
                       f"(1u << {probe_block(protocol)[1]['capability_bit']})")]),
                  "", "#endif"])
    return "\n".join(lines) + "\n"


def aligned_defines(pairs):
    """#define lines aligned the way uncrustify aligns one block."""
    width = max(len(name) for name, _ in pairs) + 4
    return [f"#define {name.ljust(width)}{value}" for name, value in pairs]


def c_enum(type_name, prefix, names):
    lines = ["typedef enum {"]
    lines.extend(f"    {prefix}_{name}{' = 0' if index == 0 else ''}," for index, name in enumerate(names))
    lines.extend([f"}} {type_name};", ""])
    return lines


def probe_header(protocol):
    _, probe = probe_block(protocol)
    lines = ["#ifndef BASE_COMPONENTS_GPIO_PROBE_CODES_H_", "#define BASE_COMPONENTS_GPIO_PROBE_CODES_H_", "",
             "/* Generated from helper_scripts/management_protocol.json. These values",
             " * travel over Zigbee unchanged. */", ""]
    lines += c_enum("gpio_probe_status_t", "GPIO_PROBE_STATUS", probe["status"])
    lines += c_enum("gpio_probe_class_t", "GPIO_PROBE_CLASS", probe["classes"])
    lines += c_enum("gpio_probe_pull_t", "GPIO_PROBE_PULL", probe["pulls"])
    bits = [(f"GPIO_PROBE_STATE_{name}", f"(1u << {bit})")
            for bit, name in enumerate(probe["state_bits"])]
    bits += [(f"GPIO_PROBE_WATCH_{name}", f"(1u << {bit})")
             for bit, name in enumerate(probe["watch_flags"])]
    limits = [(f"GPIO_PROBE_{name}", f"{value}u") for name, value in probe["limits"].items()]
    limits += [("GPIO_PROBE_WATCH_HEADER_SIZE", f"{record_size(probe['watch_header'])}u"),
               ("GPIO_PROBE_WATCH_RECORD_SIZE", f"{record_size(probe['watch_record'])}u")]
    # uncrustify aligns both groups as one block across the blank line.
    defines = aligned_defines(bits + limits)
    lines += defines[:len(bits)] + [""] + defines[len(bits):]
    lines.extend(["", "#endif"])
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
        if "probe" in cluster:
            for attr_id, attr_name, _, _, _, _ in cluster["probe"]["attributes"]:
                lines.append(f"ZCL_ATTR_{prefix}_{attr_name} = 0x{attr_id:04X}")
            for cmd_id, cmd_name, _ in cluster["probe"]["commands"]:
                lines.append(f"ZCL_CMD_{prefix}_{cmd_name} = 0x{cmd_id:02X}")
        lines.append("")
    _, probe = probe_block(protocol)
    lines.append(f"DIAGNOSTICS_CAPABILITY_PROBE = 1 << {probe['capability_bit']}")
    for group, prefix in (("status", "GPIO_PROBE_STATUS"), ("classes", "GPIO_PROBE_CLASS"),
                          ("pulls", "GPIO_PROBE_PULL")):
        for value, name in enumerate(probe[group]):
            lines.append(f"{prefix}_{name} = {value}")
    for group, prefix in (("state_bits", "GPIO_PROBE_STATE"), ("watch_flags", "GPIO_PROBE_WATCH")):
        for bit, name in enumerate(probe[group]):
            lines.append(f"{prefix}_{name} = 1 << {bit}")
    for name, value in probe["limits"].items():
        lines.append(f"GPIO_PROBE_{name} = {value}")
    lines.append("")
    return "\n".join(lines)


def z2m_parameter_type(kind):
    # A list of records goes on the wire as raw bytes the caller packs.
    return "Zcl.BuffaloZclDataType.LIST_UINT8" if kind.startswith("{") else Z2M_PARAMETER_TYPES[kind]


def z2m_attributes(attributes, indent):
    lines = []
    for attr_id, name, _, z2m_type, access, _ in attributes:
        write = ", write: true" if access == "rw" else ""
        lines.append(f"{indent}{camel(name)}: {{name: \"{camel(name)}\", ID: 0x{attr_id:04X}, type: Zcl.DataType.{z2m_type}{write}}},")
    return lines


def z2m_template(protocol):
    """`managementProtocol`: cluster ids, attribute and probe command definitions
    in the shape `deviceAddCustomCluster` takes."""
    config, (diagnostics, probe) = config_cluster(protocol), probe_block(protocol)
    lines = ["// Generated from helper_scripts/management_protocol.json.",
             "const managementProtocol = {",
             f"    manufacturerCode: 0x{protocol['manufacturer_code']:04X},",
             "    deviceConfig: {", f"        ID: 0x{config['id']:04X},", "        attributes: {"]
    lines += z2m_attributes(config["attributes"], " " * 12)
    lines += ["        },", "        enums: {"]
    for key, names in config["enums"].items():
        lines.append(f"            {camel(key)}: [{', '.join(repr(n.lower()) for n in names)}],")
    lines += ["        },", "    },", "    deviceDiagnostics: {", f"        ID: 0x{diagnostics['id']:04X},",
              "        attributes: {"]
    lines += z2m_attributes(diagnostics["attributes"], " " * 12)
    lines += ["        },", "    },", "    probe: {", f"        capabilityBit: {probe['capability_bit']},",
              "        attributes: {"]
    lines += z2m_attributes(probe["attributes"], " " * 12)
    lines += ["        },", "        commands: {"]
    for cmd_id, name, fields in probe["commands"]:
        parameters = ", ".join(f"{{name: \"{field}\", type: {z2m_parameter_type(kind)}}}"
                               for field, kind in fields)
        lines.append(f"            {camel(name)}: {{name: \"{camel(name)}\", ID: 0x{cmd_id:02X}, parameters: [{parameters}]}},")
    lines += ["        },", "    },", "};", ""]
    return "\n".join(lines)


def zha_enum_class(key):
    return "DeviceConfig" + "".join(part.capitalize() for part in key.lower().split("_"))


def zha_attributes(variable, attributes, enums):
    lines = [f"{variable} = ("]
    for attr_id, name, _, z2m_type, access, _ in attributes:
        kind = zha_enum_class(name) if name in enums else ZIGPY_TYPES[z2m_type]
        lines.append(f"    (0x{attr_id:04X}, \"{name.lower()}\", {kind}, \"{access}\"),")
    return lines + [")"]


def zha_template(protocol):
    """Management attribute tables as (id, name, zigpy type, access) tuples and the
    DEVICE_CONFIG enums as zigpy enum8 classes."""
    config, (diagnostics, _) = config_cluster(protocol), probe_block(protocol)
    lines = ["# Generated from helper_scripts/management_protocol.json.",
             f"MANAGEMENT_PROTOCOL = {{'manufacturer_code': 0x{protocol['manufacturer_code']:04X}, "
             f"'device_config': 0x{config['id']:04X}, 'device_diagnostics': 0x{diagnostics['id']:04X}}}",
             ""]
    for key, names in config["enums"].items():
        lines.append(f"class {zha_enum_class(key)}(t.enum8):")
        lines += [f"    {name.lower()} = {value}" for value, name in enumerate(names)]
        lines.append("")
    lines += zha_attributes("DEVICE_CONFIG_ATTRIBUTES", config["attributes"], config["enums"]) + [""]
    lines += zha_attributes("DEVICE_DIAGNOSTICS_ATTRIBUTES", diagnostics["attributes"], {}) + [""]
    return "\n".join(lines)


def render(protocol):
    return {"header": c_header(protocol), "xml": xml(protocol),
            "probe": probe_header(protocol),
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
