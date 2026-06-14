"""SM83 (Game Boy CPU) instruction decoder.

Decodes raw bytes into Insn records (mnemonic, operands, length). The SM83 opcode
map is highly regular, so this decodes algorithmically from the encoding fields
rather than via a 512-entry literal table — see the Pan Docs opcode layout.

This module is intentionally complete for *decoding* (length + identity of every
opcode), which is what recursive disassembly needs to walk control flow. The C
*translation* of each form lives in translate.py.
"""
from __future__ import annotations
from dataclasses import dataclass

R8 = ["b", "c", "d", "e", "h", "l", "[hl]", "a"]   # table r[y]
RP = ["bc", "de", "hl", "sp"]                        # table rp[p]
RP2 = ["bc", "de", "hl", "af"]                       # table rp2[p] (push/pop)
CC = ["nz", "z", "nc", "c"]                          # condition codes cc[y]
ALU = ["add", "adc", "sub", "sbc", "and", "xor", "or", "cp"]  # alu[y]
ROT = ["rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl"]  # rot[y]


# Base-opcode T-cycle costs (Pan Docs). For conditional jr/jp/call/ret these are the
# NOT-taken cost; emit.py adds the taken penalty. CB-prefix costs handled below.
CYCLES = [
    4, 12, 8, 8, 4, 4, 8, 4, 20, 8, 8, 8, 4, 4, 8, 4,
    4, 12, 8, 8, 4, 4, 8, 4, 12, 8, 8, 8, 4, 4, 8, 4,
    8, 12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
    8, 12, 8, 8, 12, 12, 12, 4, 8, 8, 8, 8, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    8, 12, 12, 16, 12, 16, 8, 16, 8, 16, 12, 4, 12, 24, 8, 16,
    8, 12, 12, 0, 12, 16, 8, 16, 8, 16, 12, 0, 12, 0, 8, 16,
    12, 12, 8, 0, 0, 16, 8, 16, 16, 4, 16, 0, 0, 0, 8, 16,
    12, 12, 8, 4, 0, 16, 8, 16, 12, 8, 16, 4, 0, 0, 8, 16,
]


def insn_cycles(op: int, cb: int | None) -> int:
    if op == 0xCB:
        # CB ops: 8 T, except those touching [hl]: 16 (bit n,[hl] is 12).
        z = cb & 7
        if z != 6:
            return 8
        return 12 if (cb >> 6) == 1 else 16
    return CYCLES[op]


@dataclass(frozen=True)
class Insn:
    addr: int
    length: int
    mnemonic: str
    operands: tuple = ()
    imm: int | None = None       # immediate value (n / nn / e), if any
    cycles: int = 4              # base (not-taken) T-cycles
    # control-flow classification, used by disasm.py:
    is_jump: bool = False        # jp / jr
    is_call: bool = False        # call / rst
    is_ret: bool = False         # ret / reti
    is_terminal: bool = False    # unconditional end of basic block
    target: int | None = None    # static branch target if known (absolute)

    def __str__(self) -> str:
        ops = ", ".join(str(o) for o in self.operands)
        return f"{self.mnemonic} {ops}".strip()


def _imm8(b, i):  return b[i + 1]
def _imm16(b, i): return b[i + 1] | (b[i + 2] << 8)
def _e8(b, i):
    v = b[i + 1]
    return v - 256 if v >= 128 else v


def decode(buf: bytes, i: int, base_addr: int) -> Insn:
    """Decode one instruction at buf[i]; base_addr is the GB address of buf[i]."""
    import dataclasses
    ins = _decode(buf, i, base_addr)
    op = buf[i]
    cyc = insn_cycles(op, buf[i + 1] if op == 0xCB else None)
    return dataclasses.replace(ins, cycles=cyc)


def _decode(buf: bytes, i: int, base_addr: int) -> Insn:
    op = buf[i]
    a = base_addr

    if op == 0xCB:
        cb = buf[i + 1]
        x, y, z = cb >> 6, (cb >> 3) & 7, cb & 7
        if x == 0:
            return Insn(a, 2, ROT[y], (R8[z],))
        kind = {1: "bit", 2: "res", 3: "set"}[x]
        return Insn(a, 2, kind, (y, R8[z]))

    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1

    # --- x = 0 : misc / control / 16-bit & 8-bit loads / inc-dec / rot-a ----------
    if x == 0:
        if op == 0x00: return Insn(a, 1, "nop")
        if op == 0x10: return Insn(a, 2, "stop")
        if op == 0x76: return Insn(a, 1, "halt", is_terminal=False)
        if op == 0x08: return Insn(a, 3, "ld", ("[$%04x]" % _imm16(buf, i), "sp"), imm=_imm16(buf, i))
        if op == 0x18:  # jr e
            tgt = (a + 2 + _e8(buf, i)) & 0xFFFF
            return Insn(a, 2, "jr", (tgt,), imm=_e8(buf, i), is_jump=True, is_terminal=True, target=tgt)
        if z == 0 and 4 <= y <= 7:  # jr cc, e
            tgt = (a + 2 + _e8(buf, i)) & 0xFFFF
            return Insn(a, 2, "jr", (CC[y - 4], tgt), imm=_e8(buf, i), is_jump=True, target=tgt)
        if z == 1 and q == 0: return Insn(a, 3, "ld", (RP[p], "$%04x" % _imm16(buf, i)), imm=_imm16(buf, i))
        if z == 1 and q == 1: return Insn(a, 1, "add", ("hl", RP[p]))
        if z == 2:  # indirect loads via bc/de/hl+/hl-
            forms = [("[bc]", "a"), ("[de]", "a"), ("[hl+]", "a"), ("[hl-]", "a")]
            dst, src = forms[p]
            return Insn(a, 1, "ld", (dst, src) if q == 0 else (src, dst))
        if z == 3: return Insn(a, 1, "inc" if q == 0 else "dec", (RP[p],))
        if z == 4: return Insn(a, 1, "inc", (R8[y],))
        if z == 5: return Insn(a, 1, "dec", (R8[y],))
        if z == 6: return Insn(a, 2, "ld", (R8[y], "$%02x" % _imm8(buf, i)), imm=_imm8(buf, i))
        if z == 7:
            return Insn(a, 1, ["rlca", "rrca", "rla", "rra", "daa", "cpl", "scf", "ccf"][y])

    # --- x = 1 : ld r,r' (and halt handled above) ---------------------------------
    if x == 1:
        return Insn(a, 1, "ld", (R8[y], R8[z]))

    # --- x = 2 : alu a, r ---------------------------------------------------------
    if x == 2:
        return Insn(a, 1, ALU[y], (R8[z],))

    # --- x = 3 : assorted ---------------------------------------------------------
    if x == 3:
        if z == 0:
            if y < 4: return Insn(a, 1, "ret", (CC[y],), is_ret=True)
            if op == 0xE0: return Insn(a, 2, "ldh", ("[$ff%02x]" % _imm8(buf, i), "a"), imm=_imm8(buf, i))
            if op == 0xF0: return Insn(a, 2, "ldh", ("a", "[$ff%02x]" % _imm8(buf, i)), imm=_imm8(buf, i))
            if op == 0xE8: return Insn(a, 2, "add", ("sp", _e8(buf, i)), imm=_e8(buf, i))
            if op == 0xF8: return Insn(a, 2, "ld", ("hl", "sp+%d" % _e8(buf, i)), imm=_e8(buf, i))
        if z == 1:
            if q == 0: return Insn(a, 1, "pop", (RP2[p],))
            if p == 0: return Insn(a, 1, "ret", is_ret=True, is_terminal=True)
            if p == 1: return Insn(a, 1, "reti", is_ret=True, is_terminal=True)
            if p == 2: return Insn(a, 1, "jp", ("hl",), is_jump=True, is_terminal=True)  # jp hl (computed)
            if p == 3: return Insn(a, 1, "ld", ("sp", "hl"))
        if z == 2:
            if y < 4:  # jp cc, nn
                return Insn(a, 3, "jp", (CC[y], _imm16(buf, i)), imm=_imm16(buf, i), is_jump=True, target=_imm16(buf, i))
            forms = {0xE2: ("[$ff00+c]", "a"), 0xF2: ("a", "[$ff00+c]"),
                     0xEA: ("[$%04x]" % _imm16(buf, i), "a"), 0xFA: ("a", "[$%04x]" % _imm16(buf, i))}
            if op in (0xEA, 0xFA): return Insn(a, 3, "ld", forms[op], imm=_imm16(buf, i))
            return Insn(a, 1, "ld", forms[op])
        if z == 3:
            if op == 0xC3:  # jp nn
                return Insn(a, 3, "jp", (_imm16(buf, i),), imm=_imm16(buf, i), is_jump=True, is_terminal=True, target=_imm16(buf, i))
            if op == 0xF3: return Insn(a, 1, "di")
            if op == 0xFB: return Insn(a, 1, "ei")
        if z == 4 and y < 4:  # call cc, nn
            return Insn(a, 3, "call", (CC[y], _imm16(buf, i)), imm=_imm16(buf, i), is_call=True, target=_imm16(buf, i))
        if z == 5:
            if q == 0: return Insn(a, 1, "push", (RP2[p],))
            if op == 0xCD:  # call nn
                return Insn(a, 3, "call", (_imm16(buf, i),), imm=_imm16(buf, i), is_call=True, target=_imm16(buf, i))
        if z == 6:  # alu a, n
            return Insn(a, 2, ALU[y], ("$%02x" % _imm8(buf, i),), imm=_imm8(buf, i))
        if z == 7:  # rst
            tgt = y * 8
            return Insn(a, 1, "rst", (tgt,), is_call=True, target=tgt)

    return Insn(a, 1, "db", ("$%02x" % op,))  # undefined / data byte


if __name__ == "__main__":
    # sanity: decode a tiny snippet
    sample = bytes([0x00, 0x3E, 0x42, 0xC3, 0x50, 0x01, 0xCB, 0x11])
    pc = 0
    while pc < len(sample):
        ins = decode(sample, pc, 0x4000 + pc)
        print(f"{ins.addr:04x}: {ins}  (len {ins.length})")
        pc += ins.length
