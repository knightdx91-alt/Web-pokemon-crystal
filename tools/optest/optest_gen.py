#!/usr/bin/env python3
"""Generate optest_gen.c: a single-instruction executor exec_op() for all 512 opcodes.

Reuses translate.py (in IMM_FROM_MEM mode) for the data/ALU ops so the harness tests
the REAL recompiler semantics, and emits control-flow ops with runtime targets. The
SingleStepTests (sm83) driver sets CPU+RAM, calls exec_op() once, and compares.
"""
from __future__ import annotations
import translate
from decode import _decode

I16 = "(uint16_t)(bus_read((uint16_t)(pc0 + 1)) | (bus_read((uint16_t)(pc0 + 2)) << 8))"
E8 = "((int8_t)bus_read((uint16_t)(pc0 + 1)))"
_COND = {"nz": "!GET_FLAG(FLAG_Z)", "z": "GET_FLAG(FLAG_Z)",
         "nc": "!GET_FLAG(FLAG_C)", "c": "GET_FLAG(FLAG_C)"}


def _cond(ins):
    if ins.operands and isinstance(ins.operands[0], str) and ins.operands[0] in _COND:
        return _COND[ins.operands[0]]
    return None


def control_stmts(ins) -> list[str]:
    m, ops = ins.mnemonic, ins.operands
    c = _cond(ins)
    nxt = f"0x{ins.addr + ins.length:04x}"   # addr is 0 here, so length-based fallthrough
    if m == "jp":
        if ops and ops[0] == "hl":
            return ["cpu.pc = HL();"]
        tgt = I16
        return [f"cpu.pc = {c} ? {tgt} : (uint16_t)(pc0 + 3);"] if c else [f"cpu.pc = {tgt};"]
    if m == "jr":
        tgt = "(uint16_t)(pc0 + 2 + " + E8 + ")"
        return [f"cpu.pc = {c} ? {tgt} : (uint16_t)(pc0 + 2);"] if c else [f"cpu.pc = {tgt};"]
    if m == "call":
        tgt = I16
        if c:
            return [f"if ({c}) {{ push16((uint16_t)(pc0 + 3)); cpu.pc = {tgt}; }}"
                    f" else cpu.pc = (uint16_t)(pc0 + 3);"]
        return [f"push16((uint16_t)(pc0 + 3)); cpu.pc = {tgt};"]
    if m == "rst":
        return [f"push16((uint16_t)(pc0 + 1)); cpu.pc = 0x{ins.target:04x};"]
    if m in ("ret", "reti"):
        pre = "cpu.ime = 1; " if m == "reti" else ""
        if c:
            return [f"{pre}if ({c}) cpu.pc = pop16(); else cpu.pc = (uint16_t)(pc0 + 1);"]
        return [f"{pre}cpu.pc = pop16();"]
    return ["/* unhandled control */"]


def body_for(opbytes: bytes) -> list[str]:
    ins = _decode(opbytes, 0, 0)   # addr 0; immediates resolved from memory at runtime
    if ins.is_jump or ins.is_call or ins.is_ret:
        return control_stmts(ins)
    if ins.mnemonic == "halt":
        return ["cpu.halted = 1; cpu.pc = (uint16_t)(pc0 + 1);"]
    if ins.mnemonic == "db":       # undefined opcode: leave state, advance 1
        return ["cpu.pc = (uint16_t)(pc0 + 1);"]
    stmts = translate.translate(ins)
    stmts.append(f"cpu.pc = (uint16_t)(pc0 + {ins.length});")
    return stmts


def main():
    translate.IMM_FROM_MEM = True
    out = ['/* optest_gen.c — generated single-instruction executor (do not edit) */',
           '#include "gb.h"', '#include "hal_internal.h"', "",
           "void exec_op(void) {",
           "  uint16_t pc0 = cpu.pc;",
           "  uint8_t op = bus_read(pc0);",
           "  switch (op) {"]
    for op in range(256):
        if op == 0xCB:
            out.append("  case 0xCB: {")
            out.append("    uint8_t cb = bus_read((uint16_t)(pc0 + 1));")
            out.append("    switch (cb) {")
            for cb in range(256):
                out.append(f"    case 0x{cb:02x}:")
                for s in translate.translate(_decode(bytes([0xCB, cb]), 0, 0)):
                    out.append(f"      {s}")
                out.append("      break;")
            out += ["    }",
                    "    cpu.pc = (uint16_t)(pc0 + 2); break; }"]
            continue
        out.append(f"  case 0x{op:02x}:")
        for s in body_for(bytes([op, 0, 0])):
            out.append(f"    {s}")
        out.append("    break;")
    out += ["  }", "}"]
    print("\n".join(out))


if __name__ == "__main__":
    main()
