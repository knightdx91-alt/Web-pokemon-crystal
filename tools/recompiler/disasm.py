"""Recursive-descent disassembly of the ROM, seeded by symbols + hardware vectors.

Produces the set of basic blocks per bank that emit.py turns into the dispatch
switch. Walking from known entry points (not linear sweep) keeps us out of data
regions; symbols from the .sym fill in targets reached only via computed jumps.
See ARCHITECTURE.md §3.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from decode import decode, Insn
from symfile import SymbolTable

# Fixed code entry points every Game Boy program has.
HARD_VECTORS = [0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,  # rst
                0x40, 0x48, 0x50, 0x58, 0x60,                    # interrupts
                0x0100]                                          # entry


@dataclass
class Block:
    bank: int
    start: int
    insns: list[Insn] = field(default_factory=list)
    succ: list[int] = field(default_factory=list)  # successor addrs (same bank)


def bank_window(bank: int) -> tuple[int, int]:
    """GB address range a bank's bytes are visible at."""
    return (0x0000, 0x4000) if bank == 0 else (0x4000, 0x8000)


def rom_offset(bank: int, addr: int) -> int:
    lo, _ = bank_window(bank)
    return bank * 0x4000 + (addr - lo)


def disassemble_bank(rom: bytes, bank: int, syms: SymbolTable) -> dict[int, Block]:
    lo, hi = bank_window(bank)
    seeds = {s.addr for s in syms.code_seeds() if s.bank == bank and lo <= s.addr < hi}
    if bank == 0:
        seeds |= {v for v in HARD_VECTORS if lo <= v < hi}

    blocks: dict[int, Block] = {}
    worklist = sorted(seeds)
    seen: set[int] = set()

    while worklist:
        start = worklist.pop()
        if start in seen:
            continue
        seen.add(start)
        blk = Block(bank, start)
        addr = start
        while lo <= addr < hi:
            off = rom_offset(bank, addr)
            if off + 3 > len(rom):
                break
            ins = decode(rom, off, addr)
            blk.insns.append(ins)
            nxt = addr + ins.length
            if ins.is_terminal:
                if ins.target is not None and lo <= ins.target < hi:
                    blk.succ.append(ins.target); worklist.append(ins.target)
                break
            if ins.is_ret:
                break
            if ins.is_jump or ins.is_call:
                if ins.target is not None and lo <= ins.target < hi:
                    blk.succ.append(ins.target); worklist.append(ins.target)
                # conditional jump/call falls through to nxt
            if nxt in seeds or nxt in seen:
                blk.succ.append(nxt); worklist.append(nxt)
                break
            addr = nxt
        blocks[start] = blk
    return blocks


if __name__ == "__main__":
    import sys
    rom = open(sys.argv[1], "rb").read()
    syms = SymbolTable.parse(sys.argv[2])
    b = disassemble_bank(rom, 0, syms)
    print(f"bank 0: {len(b)} basic blocks")
