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


def _emit_branch(ins: Insn, lines: list[str]) -> None:
    """Emit C for a control-flow instruction. Every taken path ends in `return;`,
    handing control back to the trampoline with cpu.pc set."""
    m = ins.mnemonic
    cond = None
    if ins.operands and isinstance(ins.operands[0], str) and ins.operands[0] in ("nz", "z", "nc", "c"):
        cond = {"nz": "!GET_FLAG(FLAG_Z)", "z": "GET_FLAG(FLAG_Z)",
                "nc": "!GET_FLAG(FLAG_C)", "c": "GET_FLAG(FLAG_C)"}[ins.operands[0]]

    pen = _TAKEN_PENALTY.get(m, 0) if cond else 0          # penalty only when conditional
    tick = f"cpu.cycles += {pen}; " if pen else ""

    def guarded(body: str) -> str:
        return f"if ({cond}) {{ {tick}{body} }}" if cond else body

    if m in ("jp", "jr"):
        if ins.target is not None:
            lines.append("    " + guarded(f"cpu.pc = 0x{ins.target:04x}; return;"))
        else:  # jp hl (computed) — target resolved at runtime; trampoline routes it
            lines.append("    cpu.pc = HL(); return;")
    elif m == "call":
        ret = ins.addr + ins.length
        if ins.target is not None:
            lines.append("    " + guarded(f"push16(0x{ret:04x}); cpu.pc = 0x{ins.target:04x}; return;"))
    elif m == "rst":
        lines.append(f"    push16(0x{ins.addr + ins.length:04x}); cpu.pc = 0x{ins.target:04x}; return;")
    elif m in ("ret", "reti"):
        if m == "reti":
            lines.append("    cpu.ime = 1;")
        lines.append("    " + guarded("cpu.pc = pop16(); return;"))


def emit_block(blk: Block) -> list[str]:
    # Base T-cycles for the block charged up front; conditional-branch taken penalties
    # are added inline by _emit_branch.
    base = sum(ins.cycles for ins in blk.insns)
    out = [f"  case 0x{blk.start:04x}:", f"    cpu.cycles += {base};"]
    for ins in blk.insns:
        if ins.is_jump or ins.is_call or ins.is_ret:
            _emit_branch(ins, out)
        elif ins.mnemonic == "halt":
            # suspend until interrupt; save resume pc and hand back to the frame loop
            out.append(f"    cpu.halted = 1; cpu.pc = 0x{ins.addr + ins.length:04x}; return;")
        else:
            for c in translate(ins):
                out.append(f"    {c}")
    # fall-through to the next address if the last instruction wasn't a transfer
    last = blk.insns[-1] if blk.insns else None
    if last and not (last.is_terminal or last.is_ret):
        out.append(f"    cpu.pc = 0x{last.addr + last.length:04x}; return;")
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
