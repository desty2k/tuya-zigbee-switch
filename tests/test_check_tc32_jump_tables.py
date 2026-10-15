"""helper_scripts/check_tc32_jump_tables.py fails closed on indirect jumps.

Each case is a synthetic image: a disassembly in tc32-elf-objdump format and an
ELF whose one allocated section holds the literal pool and the case table.
"""

import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "helper_scripts/check_tc32_jump_tables.py"
BASE = 0x1000
LITERAL = 0x1010
TABLE = 0x1014
CODE_TARGETS = (0x100C, 0x100E)


def elf_image(path: Path, words: dict[int, int]) -> Path:
    data = bytearray(0x20)
    for addr, value in words.items():
        struct.pack_into("<I", data, addr - BASE, value)
    header = bytearray(52)
    header[0:7] = b"\x7fELF\x01\x01\x01"
    shoff = len(header) + len(data)
    struct.pack_into("<I", header, 0x20, shoff)
    struct.pack_into("<HH", header, 0x2E, 40, 2)
    null_section = bytes(40)
    # sh_name, sh_type PROGBITS, sh_flags ALLOC|EXECINSTR, sh_addr, sh_offset, sh_size
    section = struct.pack("<10I", 0, 1, 6, BASE, len(header), len(data), 0, 0, 4, 0)
    path.write_bytes(bytes(header) + bytes(data) + null_section + section)
    return path


def run(tmp_path: Path, dispatch: list[str], table: tuple[int, int] = CODE_TARGETS,
        extra: list[str] = ()) -> subprocess.CompletedProcess:
    lines = ["00001000 <f>:",
             "    1000:\t2801      \ttcmp\tr0, #1",
             "    1002:\t0080      \ttshftls\tr0, r0, #2",
             "    1004:\t0b02      \ttloadr\tr3, [pc, #8]\t; (1010 <f+0x10>)"]
    lines += dispatch
    lines += ["    100c:\t2000      \ttmovs\tr0, #0",
              "    100e:\t0770      \ttjex\tlr"]
    lines += list(extra)
    disassembly = tmp_path / "image.dis"
    disassembly.write_text("\n".join(lines) + "\n")
    elf = elf_image(tmp_path / "image.elf",
                    {LITERAL: TABLE, TABLE: table[0], TABLE + 4: table[1]})
    return subprocess.run([sys.executable, str(SCRIPT), str(elf),
                           "--disassembly", str(disassembly)],
                          capture_output=True, text=True)


ADJACENT = ["    1006:\t5818      \ttloadr\tr3, [r3, r0]",
            "    1008:\t469f      \ttmov\tpc, r3",
            "    100a:\t46c0      \ttnop"]
SEPARATED = ["    1006:\t5818      \ttloadr\tr3, [r3, r0]",
             "    1008:\t2100      \ttmovs\tr1, #0",
             "    100a:\t469f      \ttmov\tpc, r3"]
COPIED_TJEX = ["    1006:\t5818      \ttloadr\tr2, [r3, r0]",
               "    1008:\t4694      \ttmov\tip, r2",
               "    100a:\t4760      \ttjex\tip"]
COMPUTED = ["    1006:\t1818      \ttadds\tr3, r3, r0",
            "    1008:\t2100      \ttmovs\tr1, #0",
            "    100a:\t469f      \ttmov\tpc, r3"]
BYTE_TABLE = (0x07051D03, 0x07051D03)


@pytest.mark.parametrize("dispatch", [ADJACENT, SEPARATED, COPIED_TJEX])
def test_valid_table_dispatch_passes(tmp_path: Path, dispatch: list[str]) -> None:
    result = run(tmp_path, dispatch)
    assert result.returncode == 0, result.stderr
    assert "1 table dispatches checked, all valid" in result.stdout


@pytest.mark.parametrize("dispatch", [ADJACENT, SEPARATED, COPIED_TJEX])
def test_byte_offset_table_fails(tmp_path: Path, dispatch: list[str]) -> None:
    result = run(tmp_path, dispatch, table=BYTE_TABLE)
    assert result.returncode == 1
    assert "malformed switch table in f" in result.stderr


def test_unclassified_indirect_jump_fails(tmp_path: Path) -> None:
    result = run(tmp_path, COMPUTED)
    assert result.returncode == 1
    assert "indirect jump at 0x100a in f" in result.stderr


def test_call_trampolines_and_returns_are_not_dispatches(tmp_path: Path) -> None:
    trampoline = ["00001020 <g>:",
                  "    1020:\t5851      \ttloadr\tr1, [r2, r1]",
                  "    1022:\tf000 f801 \ttjl\t1028 <g+0x8>",
                  "    1026:\tbd08      \ttpop\t{r3}",
                  "    1028:\t4708      \ttjex\tr1",
                  "    102a:\t4718      \ttjex\tr3"]
    result = run(tmp_path, ADJACENT, extra=trampoline)
    assert result.returncode == 0, result.stderr
    assert "1 table dispatches checked, all valid" in result.stdout
