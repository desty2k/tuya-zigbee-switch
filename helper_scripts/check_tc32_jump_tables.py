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
resets. This script classifies every indirect jump in the image ("tmov pc,rN"
and "tjex rN" other than "tjex lr") by the instruction that last wrote rN,
following register copies back to the start of the function:

- a "tjex rN" that is itself the target of a call or branch is a libgcc-style
  call trampoline (the caller loaded a function pointer); it is not checked;
- a "tpop {..., rN}" makes the jump a function return;
- a register-indexed "tloadr rN,[rT,rI]" makes it a table dispatch: rT is
  traced back to its literal-pool load (through register copies), the table
  entries are read for each case index up to the preceding bounds check, and
  each entry must be the address of an instruction inside the same function.

Any other indirect jump, and any dispatch whose table base cannot be traced,
fails the check, so a dispatch shape this script does not know cannot pass.

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
    with open(path, 'rb') as f:
        raw = f.read()
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


REGISTER = r'(?:r\d+|sl|fp|ip)'
# Ops whose first operand is not a destination register.
NON_WRITING_OPS = ('tcmp', 'tcmn', 'ttst', 'tstorer', 'tstorem', 'tpush', 'tj', 'tnop')


def writes(op, args, register):
    """Whether the instruction writes `register`."""
    if op.startswith(('tpop', 'tloadm')):
        return re.search(r'[{ ,]' + register + r'[},]', args) is not None
    if op.startswith(NON_WRITING_OPS):
        return False
    return args.startswith(register + ',')


def register_source(insns, order, index, register, start):
    """Order index of the instruction that last wrote `register` before
    order[index], following register copies; None when not found."""
    for i in range(index - 1, -1, -1):
        addr = order[i]
        if addr < start:
            return None
        op, args, _, _ = insns[addr]
        if not writes(op, args, register):
            continue
        m = re.match(r'\w+, (' + REGISTER + r')$', args)
        if op == 'tmov' and m:
            register = m[1]
            continue
        return i
    return None


def branch_targets(insns, order):
    targets = set()
    for addr in order:
        op, args, _, _ = insns[addr]
        m = re.match(r'([0-9a-f]+) <', args)
        if op.startswith('tj') and m:
            targets.add(int(m[1], 16))
    return targets


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
        m = re.match(r'\w+, (' + REGISTER + r')$', args)
        if op == 'tmov' and m:
            register = m[1]
            continue
        return None
    return None


def indirect_jump_register(op, args):
    m = re.match(r'pc, (' + REGISTER + r')$', args) if op == 'tmov' else None
    if op == 'tjex':
        m = re.match(r'(' + REGISTER + r')$', args)
    return m[1] if m else None


def scan(segments, functions, insns, order):
    bad = []
    unresolved = []
    total = 0
    targets = branch_targets(insns, order)
    for i, addr in enumerate(order):
        op, args, _, func = insns[addr]
        register = indirect_jump_register(op, args)
        if register is None or (op == 'tjex' and addr in targets):
            continue
        start, end = function_bounds(functions, func[0]) if func else (0, 1 << 32)
        name = func[1] if func else '?'
        source = register_source(insns, order, i, register, start)
        sop, sargs = insns[order[source]][:2] if source is not None else ('', '')
        if sop.startswith('tpop'):
            continue
        total += 1
        lm = re.match(r'(' + REGISTER + r'), \[(' + REGISTER + r'), (' + REGISTER + r')\]$',
                      sargs)
        literal = None
        if sop == 'tloadr' and lm:
            literal = table_literal(insns, order, source, lm[2], start)
        if literal is None:
            unresolved.append((name, addr))
            continue
        table = read_word(segments, literal)
        for case in range(case_bound(insns, order, source + 1) + 1):
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
    # An empty or foreign disassembly would otherwise pass with nothing checked.
    if not functions or not insns:
        sys.exit(f'{args.elf}: no tc32 instructions parsed from the disassembly')
    total, bad, unresolved = scan(segments, functions, insns, order)
    for name, addr in unresolved:
        print(f'{args.elf}: indirect jump at {addr:#x} in {name} is neither a '
              'return, a call trampoline nor a traced table dispatch', file=sys.stderr)
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
