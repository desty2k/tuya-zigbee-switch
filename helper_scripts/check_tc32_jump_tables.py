#!/usr/bin/env python3
"""Fail when a linked TC32 image contains a switch table its dispatch cannot use.

tc32-gcc 4.5.1 at -Os emits switch tables as byte-sized, PC-relative halfword
offsets (".byte (.Lcase-.Ltable)/2") but dispatches every table with an
absolute 32-bit load:

    tshftls rI, rI, #2
    tloadr  rT, [pc, #n]      ; literal = table address
    tloadr  rX, [rT, rI]      ; 32-bit entry
    tmov    pc, rX

A byte table read this way yields a non-code address and the CPU faults or
resets. This script finds every "tloadr rX,[rT,rI]; tmov pc,rX" dispatch in
the image, traces rT back to its literal-pool load (through register copies),
reads the table entries for each case index up to the preceding bounds check,
and requires each entry to be the address of an instruction inside the same
function. A dispatch whose table base cannot be traced also fails the check.

Usage: check_tc32_jump_tables.py IMAGE.elf [--objdump PATH | --disassembly FILE]
Exit status: 0 when every table is valid, 1 when a malformed table is found.
"""
import argparse
import re
import shutil
import struct
import subprocess
import sys

FUNC_RE = re.compile(r'^([0-9a-f]+) <([^>]+)>:$')
INSN_RE = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} ?)+)\s+(t\w+)\s*(.*)$')
LITERAL_RE = re.compile(r'\[pc, #\d+\]\s*;\s*\(([0-9a-f]+)')
MAX_LOOKBACK = 8


def load_image(path):
    raw = open(path, 'rb').read()
    if raw[:4] != b'\x7fELF' or raw[4] != 1:
        sys.exit(f'{path}: not an ELF32 file')
    shoff, = struct.unpack_from('<I', raw, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', raw, 0x2e)
    segments = []
    for i in range(shnum):
        _, sh_type, flags, addr, offset, size = struct.unpack_from(
            '<6I', raw, shoff + i * shentsize)
        if flags & 2 and sh_type != 8 and size:
            segments.append((addr, raw[offset:offset + size]))
    return segments


def read_word(segments, addr):
    for base, data in segments:
        if base <= addr and addr + 4 <= base + len(data):
            return struct.unpack_from('<I', data, addr - base)[0]
    return None


def parse_disassembly(lines):
    functions = []
    insns = {}
    order = []
    for line in lines:
        line = line.rstrip('\n')
        m = FUNC_RE.match(line)
        if m:
            functions.append((int(m[1], 16), m[2]))
            continue
        m = INSN_RE.match(line)
        if m:
            addr = int(m[1], 16)
            insns[addr] = (m[3], m[4].split(';')[0].strip(), m[4], functions[-1] if functions else None)
            order.append(addr)
    functions.sort()
    return functions, insns, order


def function_bounds(functions, start):
    for i, (addr, _) in enumerate(functions):
        if addr == start:
            return addr, functions[i + 1][0] if i + 1 < len(functions) else 1 << 32
    return start, 1 << 32


def case_bound(insns, order, index):
    """Highest case index from the nearest preceding `tcmp rN, #K`, else 0."""
    for addr in reversed(order[max(0, index - MAX_LOOKBACK):index]):
        op, args, _, _ = insns[addr]
        m = re.match(r'r\d+, #(\d+)$', args)
        if op == 'tcmp' and m:
            return int(m[1])
    return 0


def table_literal(insns, order, index, register, start):
    """Literal-pool address that loaded `register`, following register copies."""
    for addr in reversed(order[:index]):
        if addr < start:
            return None
        op, args, raw, _ = insns[addr]
        if not args.startswith(register + ','):
            continue
        m = LITERAL_RE.search(raw)
        if op == 'tloadr' and m:
            return int(m[1], 16)
        m = re.match(r'\w+, (r\d+|sl|fp|ip)$', args)
        if op == 'tmov' and m:
            register = m[1]
            continue
        return None
    return None


def scan(segments, functions, insns, order):
    bad = []
    unresolved = []
    total = 0
    for i, addr in enumerate(order):
        op, args, _, func = insns[addr]
        m = re.match(r'pc, (r\d+)$', args)
        if op != 'tmov' or not m or i < 1:
            continue
        lop, largs, _, _ = insns[order[i - 1]]
        lm = re.match(r'(r\d+), \[(r\d+), (r\d+)\]$', largs)
        if lop != 'tloadr' or not lm or lm[1] != m[1]:
            continue
        total += 1
        start, end = function_bounds(functions, func[0]) if func else (0, 1 << 32)
        name = func[1] if func else '?'
        literal = table_literal(insns, order, i - 1, lm[2], start)
        if literal is None:
            unresolved.append((name, addr))
            continue
        table = read_word(segments, literal)
        for case in range(case_bound(insns, order, i) + 1):
            entry = read_word(segments, table + 4 * case) if table is not None else None
            target = None if entry is None else entry & ~1
            if target is None or target not in insns or not start <= target < end:
                bad.append((name, addr, table, case, entry))
                break
    return total, bad, unresolved


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('elf')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--objdump', default=None)
    group.add_argument('--disassembly', default=None)
    args = parser.parse_args()

    if args.disassembly:
        with open(args.disassembly) as f:
            lines = f.readlines()
    else:
        objdump = args.objdump or shutil.which('tc32-elf-objdump')
        if not objdump:
            sys.exit('tc32-elf-objdump not found; pass --objdump or --disassembly')
        lines = subprocess.run([objdump, '-d', args.elf], check=True,
                               capture_output=True, text=True).stdout.splitlines()

    segments = load_image(args.elf)
    functions, insns, order = parse_disassembly(lines)
    total, bad, unresolved = scan(segments, functions, insns, order)
    for name, addr in unresolved:
        print(f'{args.elf}: table base not traced for dispatch at {addr:#x} in {name}',
              file=sys.stderr)
    for name, addr, table, case, entry in bad:
        entry_text = 'unreadable' if entry is None else f'{entry:#010x}'
        table_text = 'unreadable' if table is None else f'{table:#x}'
        print(f'{args.elf}: malformed switch table in {name}: dispatch at {addr:#x}, '
              f'table {table_text}, case {case} entry {entry_text} is not code in the function',
              file=sys.stderr)
    if bad or unresolved:
        print(f'{args.elf}: {len(bad)} of {total} table dispatches are malformed, '
              f'{len(unresolved)} not traced '
              '(tc32-gcc -Os jump tables; build with -fno-jump-tables)', file=sys.stderr)
        return 1
    print(f'{args.elf}: {total} table dispatches checked, all valid')
    return 0


if __name__ == '__main__':
    sys.exit(main())
