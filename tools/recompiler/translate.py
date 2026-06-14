"""SM83 Insn -> C statement(s).

Emits C that reads/writes the CpuState struct and goes through bus_read/bus_write
(see runtime/include/gb.h). This covers a representative, load-bearing subset of the
instruction set with explicit, regular patterns; the long tail is filled in opcode
group by opcode group. Anything unhandled emits a `/* TODO */ trap()` so gaps are
loud at runtime rather than silently wrong.

Control-flow instructions (jp/jr/call/ret/rst) are emitted by disasm.py/emit.py into
the per-bank dispatch switch (ARCHITECTURE.md §3.1); this module handles the
straight-line data/ALU/load ops within a basic block.
"""
from __future__ import annotations
from decode import Insn

# map SM83 r8 operand -> C lvalue/expression (memory form handled specially)
_R8_GET = {"a": "cpu.a", "b": "cpu.b", "c": "cpu.c", "d": "cpu.d",
           "e": "cpu.e", "h": "cpu.h", "l": "cpu.l", "[hl]": "bus_read(HL())"}
_R8_SET = {"a": "cpu.a", "b": "cpu.b", "c": "cpu.c", "d": "cpu.d",
           "e": "cpu.e", "h": "cpu.h", "l": "cpu.l"}


def _set_r8(dst: str, expr: str) -> str:
    if dst == "[hl]":
        return f"bus_write(HL(), (uint8_t)({expr}));"
    return f"{_R8_SET[dst]} = (uint8_t)({expr});"


def translate(ins: Insn) -> list[str]:
    """Return C statements for a non-control-flow instruction."""
    m, ops = ins.mnemonic, ins.operands

    if m == "nop":
        return ["/* nop */"]
    if m == "halt":
        return ["cpu.halted = 1; return;  /* resume via interrupt */"]
    if m in ("di", "ei"):
        return [f"cpu.ime = {0 if m == 'di' else 1};"]

    # 8-bit register/immediate loads -------------------------------------------------
    if m == "ld" and len(ops) == 2:
        dst, src = ops
        if dst in _R8_SET and (src in _R8_GET):
            return [_set_r8(dst, _R8_GET[src])]
        if dst in _R8_SET and isinstance(src, str) and src.startswith("$") and ins.imm is not None:
            return [_set_r8(dst, f"0x{ins.imm:02x}")]
        if dst == "[hl]" and src in _R8_GET:
            return [_set_r8("[hl]", _R8_GET[src])]
        # 16-bit immediate loads into pairs
        pairs = {"bc": "SET_BC", "de": "SET_DE", "hl": "SET_HL"}
        if dst in pairs and ins.imm is not None:
            return [f"{pairs[dst]}(0x{ins.imm:04x});"]
        if dst == "sp" and ins.imm is not None:
            return [f"cpu.sp = 0x{ins.imm:04x};"]
        return [f"/* TODO ld {dst},{src} */ trap();"]

    if m == "ldh" and ins.imm is not None:
        dst, src = ops
        if dst == "a":
            return [f"cpu.a = bus_read(0xFF00 + 0x{ins.imm:02x});"]
        return [f"bus_write(0xFF00 + 0x{ins.imm:02x}, cpu.a);"]

    # inc/dec 8-bit ------------------------------------------------------------------
    if m in ("inc", "dec") and len(ops) == 1 and ops[0] in _R8_GET:
        r = ops[0]
        delta = "+ 1" if m == "inc" else "- 1"
        tmp = f"(uint8_t)({_R8_GET[r]} {delta})"
        hcond = "(_v & 0x0F) == 0x0F" if m == "dec" else "(_v & 0x0F) == 0"
        lines = [f"{{ uint8_t _v = {tmp};"]
        lines.append(f"  SET_FLAG(FLAG_Z, _v == 0); SET_FLAG(FLAG_N, {1 if m=='dec' else 0});")
        lines.append(f"  SET_FLAG(FLAG_H, {hcond});")
        lines.append(f"  {_set_r8(r, '_v')} }}")
        return lines

    # inc/dec 16-bit -----------------------------------------------------------------
    if m in ("inc", "dec") and len(ops) == 1 and ops[0] in ("bc", "de", "hl", "sp"):
        r = ops[0]; d = "+ 1" if m == "inc" else "- 1"
        getr = {"bc": "BC()", "de": "DE()", "hl": "HL()", "sp": "cpu.sp"}[r]
        setr = {"bc": "SET_BC", "de": "SET_DE", "hl": "SET_HL"}.get(r)
        if setr:
            return [f"{setr}((uint16_t)({getr} {d}));"]
        return [f"cpu.sp = (uint16_t)(cpu.sp {d});"]

    # ALU a, r / a, n ----------------------------------------------------------------
    if m in ("add", "adc", "sub", "sbc", "and", "or", "xor", "cp") and len(ops) == 1:
        src = ops[0]
        if src in _R8_GET:
            rhs = _R8_GET[src]
        elif isinstance(src, str) and src.startswith("$") and ins.imm is not None:
            rhs = f"0x{ins.imm:02x}"
        else:
            return [f"/* TODO alu {m} {src} */ trap();"]
        return [f"alu_{m}({rhs});"]   # alu helpers live in runtime/hal/alu.h

    # push/pop -----------------------------------------------------------------------
    if m == "push" and ops:
        getp = {"bc": "BC()", "de": "DE()", "hl": "HL()", "af": "AF()"}[ops[0]]
        return [f"cpu.sp -= 2; bus_write(cpu.sp, {getp} & 0xFF); bus_write(cpu.sp + 1, {getp} >> 8);"]
    if m == "pop" and ops:
        setp = {"bc": "SET_BC", "de": "SET_DE", "hl": "SET_HL", "af": "SET_AF"}[ops[0]]
        return [f"{setp}(bus_read(cpu.sp) | (bus_read(cpu.sp + 1) << 8)); cpu.sp += 2;"]

    # CB rotates/bit ops -------------------------------------------------------------
    if m in ("rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl") and ops:
        return [f"cb_{m}(&{_R8_SET.get(ops[0], 'NULL')});" if ops[0] != "[hl]"
                else f"cb_{m}_hl();"]
    if m in ("bit", "res", "set") and len(ops) == 2:
        n, r = ops
        if m == "bit":
            return [f"SET_FLAG(FLAG_Z, ((({_R8_GET[r]}) >> {n}) & 1) == 0);"
                    f" SET_FLAG(FLAG_N, 0); SET_FLAG(FLAG_H, 1);"]
        mask = f"(1 << {n})"
        expr = f"{_R8_GET[r]} {'|' if m=='set' else '& ~'} {mask}"
        return [_set_r8(r, expr)]

    return [f"/* TODO {ins} */ trap();"]
