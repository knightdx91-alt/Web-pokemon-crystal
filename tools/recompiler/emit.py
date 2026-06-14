"""Turn disassembled blocks into C source.

Control-flow model (ARCHITECTURE.md §3.1, multi-bank revision): each bank becomes a
function `bank_NN(uint16_t pc)` whose `switch (pc)` cases are basic-block entries.
A block executes exactly once, sets `cpu.pc` to its successor (branch target, call
target, popped return, or fall-through), and `return`s. The central trampoline
`rom_dispatch(cpu.pc)` (generated/dispatch.c) then routes the next block to the right
bank function via a table indexed by `cpu.rom_bank` — so cross-bank calls just work:
the game sets the bank register, the table follows it. The frame loop (gb.c) calls
rom_dispatch + hal_catch_up per block, so timing/IRQs are handled between blocks.
"""
from __future__ import annotations
from decode import Insn
from disasm import Block
from translate import translate


# extra T-cycles when a conditional branch is taken (Pan Docs).
_TAKEN_PENALTY = {"jr": 4, "jp": 4, "call": 12, "ret": 12}


def _cond_present(ins: Insn) -> bool:
    return (bool(ins.operands) and isinstance(ins.operands[0], str)
            and ins.operands[0] in ("nz", "z", "nc", "c"))


def _unconditional_exit(ins: Insn) -> bool:
    """True if this instruction always leaves the block (no fall-through path)."""
    if ins.is_terminal:                       # unconditional jp/jr/ret/reti
        return True
    if ins.mnemonic == "rst":                 # unconditional call to a vector
        return True
    if ins.is_call and not _cond_present(ins):  # unconditional call
        return True
    return False


def _emit_branch(ins: Insn, acc: int, lines: list[str]) -> None:
    """Emit C for a control-flow instruction. `acc` is the T-cycles accumulated up to
    AND INCLUDING this instruction (each instruction's base = its not-taken cost), to
    be charged on the path that EXITS here. A taken conditional also adds its penalty.
    Charging at the exit (not up front) keeps cycles correct when a conditional ret/jp
    exits a block early — vital for tight VBlank timing windows."""
    m = ins.mnemonic
    cond = None
    if ins.operands and isinstance(ins.operands[0], str) and ins.operands[0] in ("nz", "z", "nc", "c"):
        cond = {"nz": "!GET_FLAG(FLAG_Z)", "z": "GET_FLAG(FLAG_Z)",
                "nc": "!GET_FLAG(FLAG_C)", "c": "GET_FLAG(FLAG_C)"}[ins.operands[0]]
    pen = _TAKEN_PENALTY.get(m, 0) if cond else 0
    charge = acc + pen   # cycles charged on the taken/exit path

    def guarded(body: str) -> str:
        return f"if ({cond}) {{ {body} }}" if cond else body

    if m in ("jp", "jr"):
        if ins.target is not None:
            lines.append("    " + guarded(f"cpu.cycles += {charge}; cpu.pc = 0x{ins.target:04x}; return;"))
        else:  # jp hl (computed) — always unconditional/terminal
            lines.append(f"    cpu.cycles += {charge}; cpu.pc = HL(); return;")
    elif m == "call":
        ret = ins.addr + ins.length
        if ins.target is not None:
            lines.append("    " + guarded(f"cpu.cycles += {charge}; push16(0x{ret:04x}); cpu.pc = 0x{ins.target:04x}; return;"))
    elif m == "rst":
        lines.append(f"    cpu.cycles += {charge}; push16(0x{ins.addr + ins.length:04x}); cpu.pc = 0x{ins.target:04x}; return;")
    elif m in ("ret", "reti"):
        body = ("cpu.ime = 1; " if m == "reti" else "") + f"cpu.cycles += {charge}; cpu.pc = pop16(); return;"
        lines.append("    " + guarded(body))


def emit_block(blk: Block) -> list[str]:
    # Accumulate T-cycles and flush them at each exit point (instead of charging the
    # whole block up front), so a mid-block conditional exit charges only what ran.
    out = [f"  case 0x{blk.start:04x}:"]
    acc = 0
    for ins in blk.insns:
        acc += ins.cycles
        if ins.is_jump or ins.is_call or ins.is_ret:
            _emit_branch(ins, acc, out)
            # not-taken path keeps `acc` (this insn's base already counted) and flows on
        elif ins.mnemonic == "halt":
            out.append(f"    cpu.cycles += {acc}; cpu.halted = 1; cpu.pc = 0x{ins.addr + ins.length:04x}; return;")
            acc = 0
        else:
            for c in translate(ins):
                out.append(f"    {c}")
    # Emit the fall-through (not-taken / straight-line) exit unless the last
    # instruction ALWAYS exits unconditionally (uncond jp/jr/ret/reti/call/rst).
    last = blk.insns[-1] if blk.insns else None
    if last and not _unconditional_exit(last):
        out.append(f"    cpu.cycles += {acc}; cpu.pc = 0x{last.addr + last.length:04x}; return;")
    return out


def emit_bank(bank: int, blocks: dict[int, Block]) -> str:
    lines = [f"/* bank {bank:02x} — generated, do not edit */",
             '#include "gb.h"',
             '#include "hal_internal.h"',
             "",
             f"void bank_{bank:02x}(uint16_t pc) {{",
             "  switch (pc) {"]
    for start in sorted(blocks):
        lines.extend(emit_block(blocks[start]))
    lines += ["  default:",
              "    /* address not a known block start: data executed as code, an",
              "       untranslated target, or a code/data-separation gap (§3.3) */",
              "    trap_pc(pc);",
              "  }",
              "}"]
    return "\n".join(lines) + "\n"
