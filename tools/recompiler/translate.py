"""SM83 Insn -> C statement(s).

Emits C that reads/writes the CpuState struct and goes through bus_read/bus_write
(see runtime/include/gb.h). Covers every non-control-flow opcode group; the
control-flow ops (jp/jr/call/ret/rst) are emitted by emit.py into the per-bank
dispatch switch (ARCHITECTURE.md §3.1).

Anything genuinely unhandled emits `/* TODO */ trap();` so gaps are loud at runtime
rather than silently wrong. The coverage test (tools/recompiler/coverage.py) asserts
the trap count for the full opcode map.
"""
from __future__ import annotations
from decode import Insn

# SM83 r8 operand -> C read expression / write target (memory form handled specially)
_R8_GET = {"a": "cpu.a", "b": "cpu.b", "c": "cpu.c", "d": "cpu.d",
           "e": "cpu.e", "h": "cpu.h", "l": "cpu.l", "[hl]": "bus_read(HL())"}
_R8_SET = {"a": "cpu.a", "b": "cpu.b", "c": "cpu.c", "d": "cpu.d",
           "e": "cpu.e", "h": "cpu.h", "l": "cpu.l"}
_PAIR_GET = {"bc": "BC()", "de": "DE()", "hl": "HL()", "sp": "cpu.sp", "af": "AF()"}
_PAIR_SET = {"bc": "SET_BC", "de": "SET_DE", "hl": "SET_HL", "af": "SET_AF"}


def _set_r8(dst: str, expr: str) -> str:
    if dst == "[hl]":
        return f"bus_write(HL(), (uint8_t)({expr}));"
    return f"{_R8_SET[dst]} = (uint8_t)({expr});"


def _set_pair(dst: str, expr: str) -> str:
    if dst == "sp":
        return f"cpu.sp = (uint16_t)({expr});"
    return f"{_PAIR_SET[dst]}((uint16_t)({expr}));"


def _is_imm(tok) -> bool:
    return isinstance(tok, str) and tok.startswith("$")


def _is_mem(tok) -> bool:
    """A bracketed absolute-address operand, e.g. '[$1234]'."""
    return isinstance(tok, str) and tok.startswith("[$") and tok.endswith("]")


def translate(ins: Insn) -> list[str]:
    """Return C statements for a non-control-flow instruction."""
    m, ops = ins.mnemonic, ins.operands

    # --- trivial / control of CPU flags ------------------------------------------
    if m == "nop":
        return ["/* nop */"]
    if m == "stop":
        return ["/* stop: CGB speed-switch handled by HAL */ cpu_stop();"]
    if m == "halt":
        return ["cpu.halted = 1; return;  /* resume via interrupt */"]
    if m in ("di", "ei"):
        return [f"cpu.ime = {0 if m == 'di' else 1};"]
    if m in ("daa", "cpl", "scf", "ccf", "rlca", "rrca", "rla", "rra"):
        return [f"op_{m}();"]   # implemented in runtime/hal/alu.c

    # --- 8-bit & 16-bit loads -----------------------------------------------------
    if m == "ld" and len(ops) == 2:
        dst, src = ops

        # reg/[hl] <- reg/[hl]
        if dst in _R8_SET and src in _R8_GET:
            return [_set_r8(dst, _R8_GET[src])]
        if dst == "[hl]" and src in _R8_GET:
            return [_set_r8("[hl]", _R8_GET[src])]
        # reg/[hl] <- imm8
        if (dst in _R8_SET or dst == "[hl]") and _is_imm(src) and ins.imm is not None:
            return [_set_r8(dst, f"0x{ins.imm:02x}")]
        # pair <- imm16
        if dst in ("bc", "de", "hl", "sp") and _is_imm(src) and ins.imm is not None:
            return [_set_pair(dst, f"0x{ins.imm:04x}")]
        # indirect via bc/de/hl+/hl-
        ind = {"[bc]": "BC()", "[de]": "DE()", "[hl+]": "HL()", "[hl-]": "HL()"}
        if dst in ind and src == "a":
            stmt = [f"bus_write({ind[dst]}, cpu.a);"]
            if dst == "[hl+]": stmt.append("SET_HL(HL() + 1);")
            if dst == "[hl-]": stmt.append("SET_HL(HL() - 1);")
            return stmt
        if src in ind and dst == "a":
            stmt = [f"cpu.a = bus_read({ind[src]});"]
            if src == "[hl+]": stmt.append("SET_HL(HL() + 1);")
            if src == "[hl-]": stmt.append("SET_HL(HL() - 1);")
            return stmt
        # high-RAM via C: ld [$ff00+c],a / ld a,[$ff00+c]
        if dst == "[$ff00+c]" and src == "a":
            return ["bus_write(0xFF00 + cpu.c, cpu.a);"]
        if dst == "a" and src == "[$ff00+c]":
            return ["cpu.a = bus_read(0xFF00 + cpu.c);"]
        # absolute: ld [nn],a / ld a,[nn]  (operands are bracketed: "[$nnnn]")
        if dst == "a" and _is_mem(src) and ins.imm is not None:
            return [f"cpu.a = bus_read(0x{ins.imm:04x});"]
        if _is_mem(dst) and src == "a" and ins.imm is not None:
            return [f"bus_write(0x{ins.imm:04x}, cpu.a);"]
        # ld [nn],sp
        if _is_mem(dst) and src == "sp" and ins.imm is not None:
            return [f"bus_write(0x{ins.imm:04x}, cpu.sp & 0xFF);",
                    f"bus_write(0x{ins.imm + 1:04x}, cpu.sp >> 8);"]
        # ld sp,hl
        if dst == "sp" and src == "hl":
            return ["cpu.sp = HL();"]
        # ld hl,sp+e
        if dst == "hl" and isinstance(src, str) and src.startswith("sp+") and ins.imm is not None:
            return [f"SET_HL(sp_offset({ins.imm}));"]   # sets H/C flags in helper
        return [f"/* TODO ld {dst},{src} */ trap();"]

    if m == "ldh" and ins.imm is not None:
        dst, _src = ops
        if dst == "a":
            return [f"cpu.a = bus_read(0xFF00 + 0x{ins.imm:02x});"]
        return [f"bus_write(0xFF00 + 0x{ins.imm:02x}, cpu.a);"]

    # --- inc / dec ----------------------------------------------------------------
    if m in ("inc", "dec") and len(ops) == 1 and ops[0] in _R8_GET:
        r = ops[0]
        delta = "+ 1" if m == "inc" else "- 1"
        hcond = "(_v & 0x0F) == 0x0F" if m == "dec" else "(_v & 0x0F) == 0"
        return [f"{{ uint8_t _v = (uint8_t)({_R8_GET[r]} {delta});",
                f"  SET_FLAG(FLAG_Z, _v == 0); SET_FLAG(FLAG_N, {1 if m=='dec' else 0});",
                f"  SET_FLAG(FLAG_H, {hcond});",
                f"  {_set_r8(r, '_v')} }}"]
    if m in ("inc", "dec") and len(ops) == 1 and ops[0] in ("bc", "de", "hl", "sp"):
        r = ops[0]; d = "+ 1" if m == "inc" else "- 1"
        return [_set_pair(r, f"{_PAIR_GET[r]} {d}")]

    # --- 16-bit add hl,rr ---------------------------------------------------------
    if m == "add" and len(ops) == 2 and ops[0] == "hl":
        return [f"add16_hl({_PAIR_GET[ops[1]]});"]
    if m == "add" and len(ops) == 2 and ops[0] == "sp" and ins.imm is not None:
        return [f"cpu.sp = sp_offset({ins.imm});"]

    # --- ALU a, r / a, n ----------------------------------------------------------
    if m in ("add", "adc", "sub", "sbc", "and", "or", "xor", "cp") and len(ops) == 1:
        src = ops[0]
        if src in _R8_GET:
            rhs = _R8_GET[src]
        elif _is_imm(src) and ins.imm is not None:
            rhs = f"0x{ins.imm:02x}"
        else:
            return [f"/* TODO alu {m} {src} */ trap();"]
        return [f"alu_{m}({rhs});"]

    # --- push / pop ---------------------------------------------------------------
    if m == "push" and ops:
        v = _PAIR_GET[ops[0]]
        return [f"cpu.sp -= 2; bus_write(cpu.sp, {v} & 0xFF); bus_write(cpu.sp + 1, {v} >> 8);"]
    if m == "pop" and ops:
        return [f"{_PAIR_SET[ops[0]]}(bus_read(cpu.sp) | (bus_read(cpu.sp + 1) << 8)); cpu.sp += 2;"]

    # --- CB rotates / shifts ------------------------------------------------------
    if m in ("rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl") and ops:
        if ops[0] == "[hl]":
            return [f"cb_{m}_hl();"]
        return [f"cb_{m}(&{_R8_SET[ops[0]]});"]
    if m in ("bit", "res", "set") and len(ops) == 2:
        n, r = ops
        if m == "bit":
            return [f"SET_FLAG(FLAG_Z, ((({_R8_GET[r]}) >> {n}) & 1) == 0);"
                    f" SET_FLAG(FLAG_N, 0); SET_FLAG(FLAG_H, 1);"]
        mask = f"(1 << {n})"
        expr = f"{_R8_GET[r]} {'|' if m == 'set' else '& ~'} {mask}"
        return [_set_r8(r, expr)]

    return [f"/* TODO {ins} */ trap();"]
