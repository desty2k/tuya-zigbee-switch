#!/usr/bin/env python3
"""Firmware version strings and OTA file versions from the VERSION file.

VERSION holds `MAJOR.MINOR.PATCH` for a release or `MAJOR.MINOR.PATCH-rc.N`
for a release candidate. The 32-bit OTA file version orders every build the
way Zigbee2MQTT and ZHA compare them (a plain integer comparison):

    byte 3  MAJOR << 4 | MINOR     (0-9 each)
    byte 2  PATCH                  (0-255)
    byte 1  stage: rc number N (1-254), 0xFF for the final release
    byte 0  build: 0 for a published build, 1-255 for development builds
            between two published ones

So 2.0.0-rc.1 < 2.0.0-rc.1 build 3 < 2.0.0-rc.2 < 2.0.0 < 2.0.1-rc.1.
Out-of-range parts are errors, never wrapped.
"""

import argparse
import re
import sys
from typing import NamedTuple

# The Basic cluster SWBuildID attribute is a 16-character string.
VERSION_STR_MAX = 16

_PATTERN = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-rc\.(\d+))?$")


class FirmwareVersion(NamedTuple):
    major: int
    minor: int
    patch: int
    rc: int | None


def parse(text: str) -> FirmwareVersion:
    match = _PATTERN.match(text.strip())
    if match is None:
        raise ValueError(f"version {text.strip()!r} is not MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-rc.N")
    major, minor, patch = (int(match.group(i)) for i in (1, 2, 3))
    rc = int(match.group(4)) if match.group(4) is not None else None
    if major > 9 or minor > 9:
        raise ValueError("major and minor must be 0-9")
    if patch > 255:
        raise ValueError("patch must be 0-255")
    if rc is not None and not 1 <= rc <= 254:
        raise ValueError("rc number must be 1-254")
    return FirmwareVersion(major, minor, patch, rc)


def file_version(version: FirmwareVersion, build: int) -> int:
    if not 0 <= build <= 255:
        raise ValueError("build must be 0-255")
    stage = 0xFF if version.rc is None else version.rc
    return (version.major << 28) | (version.minor << 24) | (version.patch << 16) | (stage << 8) | build


def version_str(text: str, git_hash: str) -> str:
    text = text.strip()
    return (f"{text}-{git_hash}" if git_hash else text)[:VERSION_STR_MAX]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version-file", default="VERSION")
    parser.add_argument("--build", type=int, default=0, help="development build number, 0 for published builds")
    parser.add_argument("--git-hash", default="")
    parser.add_argument("output", choices=["file-version", "version-str"])
    args = parser.parse_args()

    with open(args.version_file, encoding="utf-8") as f:
        text = f.read()
    try:
        version = parse(text)
        if args.output == "file-version":
            print(f"0x{file_version(version, args.build):08X}")
        else:
            print(version_str(text, args.git_hash))
    except ValueError as error:
        print(f"{args.version_file}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
