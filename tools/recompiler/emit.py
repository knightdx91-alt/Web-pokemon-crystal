"""Turn disassembled blocks into C source.

Each bank becomes a function `bank_NN(uint16_t pc)` containing a `switch (pc)` whose
cases are basic-block entry addresses (the control-flow dispatch of ARCHITECTURE.md
§3.1). Straight-line instructions inside a block come from translate.py; branches set
`pc` and re-enter the switch or call into another bank via rom_call().
"""
from __future__ import annotations
from decode import Insn
from disasm import Block
from translate import translate


# extra T-cycles when a conditional branch is taken (Pan Docs).
_TAKEN_PENALTY = {"jr": 4, "jp": 4, "call": 12, "ret": 12}


def _emit_branch(ins: Insn, lines: list[str]) -> None:
    """Emit C for a control-flow instruction at the end (or middle) of a block."""
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
            lines.append("    " + guarded(f"pc = 0x{ins.target:04x}; goto dispatch;"))
        else:  # jp hl (computed)
            lines.append("    pc = HL(); goto dispatch;")
    elif m == "call":
        ret = ins.addr + ins.length
        if ins.target is not None:
            lines.append("    " + guarded(f"push16(0x{ret:04x}); pc = 0x{ins.target:04x}; goto dispatch;"))
    elif m == "rst":
        lines.append(f"    push16(0x{ins.addr + ins.length:04x}); pc = 0x{ins.target:04x}; goto dispatch;")
    elif m in ("ret", "reti"):
        if m == "reti":
            lines.append("    cpu.ime = 1;")
        lines.append("    " + guarded("cpu.pc = pop16(); return; /* return to caller dispatcher */"))


def emit_block(blk: Block) -> list[str]:
    # Base T-cycles for the whole block charged up front; conditional-branch taken
    # penalties are added inline by _emit_branch. hal_catch_up() at dispatch keeps
    # the PPU/timers advancing during long routines (ARCHITECTURE.md §3.3).
    base = sum(ins.cycles for ins in blk.insns)
    out = [f"  case 0x{blk.start:04x}:", f"    cpu.cycles += {base};"]
    for ins in blk.insns:
        if ins.is_jump or ins.is_call or ins.is_ret:
            _emit_branch(ins, out)
        elif ins.mnemonic == "halt":
            # suspend until interrupt; save resume pc and yield to the frame loop
            out.append(f"    cpu.halted = 1; cpu.pc = 0x{ins.addr + ins.length:04x}; return;")
        else:
            for c in translate(ins):
                out.append(f"    {c}")
    # fall-through to the next block address if the last insn wasn't terminal
    last = blk.insns[-1] if blk.insns else None
    if last and not (last.is_terminal or last.is_ret):
        nxt = last.addr + last.length
        out.append(f"    pc = 0x{nxt:04x}; goto dispatch;")
    return out


def emit_bank(bank: int, blocks: dict[int, Block]) -> str:
    lines = [f"/* bank {bank:02x} — generated, do not edit */",
             '#include "gb.h"',
             '#include "hal_internal.h"',
             "",
             f"void bank_{bank:02x}(uint16_t pc) {{",
             "dispatch:",
             "  cpu.pc = pc;             /* checkpoint for resume on yield */",
             "  hal_catch_up();          /* advance PPU/APU/timers to cpu.cycles */",
             "  if (g_yield) return;     /* frame done / deadline: hand back to loop */",
             "  switch (pc) {"]
    for start in sorted(blocks):
        lines.extend(emit_block(blocks[start]))
    lines += ["  default:",
              "    /* unreached address: data executed as code, or a gap */",
              "    trap_pc(pc);",
              "  }",
              "}"]
    return "\n".join(lines) + "\n"
