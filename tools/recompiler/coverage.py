#!/usr/bin/env python3
"""Decode all 512 opcodes (256 base + 256 CB) and report translation coverage.

Control-flow ops (jp/jr/call/ret/rst) are emitted by emit.py, not translate.py, so
they're excluded here. Everything else should translate without a `trap()`. Run:

    python3 coverage.py
"""
from decode import decode, Insn
from translate import translate

CONTROL = {"jp", "jr", "call", "ret", "reti", "rst"}


def all_opcodes():
    for op in range(256):
        if op == 0xCB:
            for cb in range(256):
                yield bytes([0xCB, cb, 0, 0])
            continue
        yield bytes([op, 0x34, 0x12, 0x00])   # trailing bytes feed any immediates


def main():
    total = handled = control = traps = 0
    trap_list = []
    for buf in all_opcodes():
        ins = decode(buf, 0, 0x4000)
        if ins.mnemonic == "db":          # undefined opcode, not real instruction
            continue
        total += 1
        if ins.mnemonic in CONTROL:
            control += 1
            continue
        c = "\n".join(translate(ins))
        if "trap()" in c or "TODO" in c:
            traps += 1
            trap_list.append(str(ins))
        else:
            handled += 1

    print(f"defined opcodes:       {total}")
    print(f"  control-flow (emit): {control}")
    print(f"  translated cleanly:  {handled}")
    print(f"  TODO/trap:           {traps}")
    if trap_list:
        print("\nuntranslated:")
        for t in sorted(set(trap_list)):
            print("  ", t)
    return traps


if __name__ == "__main__":
    import sys
    sys.exit(1 if main() else 0)
