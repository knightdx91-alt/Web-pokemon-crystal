#!/usr/bin/env python3
"""Recompiler entrypoint: ROM + .sym  ->  generated/ C.

    python3 main.py --rom pokecrystal.gbc --sym pokecrystal.sym --out generated/

Drives the pipeline: symfile -> disasm (per bank) -> emit. Data banks are emitted as
byte tables; code banks as dispatch functions. See ARCHITECTURE.md §3.
"""
from __future__ import annotations
import argparse, os
from symfile import SymbolTable
from disasm import disassemble_bank
from emit import emit_bank


def emit_data_bank(rom: bytes, bank: int) -> str:
    start = bank * 0x4000
    chunk = rom[start:start + 0x4000]
    body = ",".join(str(b) for b in chunk)
    return (f"/* bank {bank:02x} data */\n#include <stdint.h>\n"
            f"const uint8_t rom_bank_{bank:02x}[0x4000] = {{{body}}};\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--sym", required=True)
    ap.add_argument("--out", default="generated")
    ap.add_argument("--code-banks", default="0",
                    help="comma list / ranges of banks to translate as code, e.g. 0,1,3-5")
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    syms = SymbolTable.parse(args.sym)
    nbanks = len(rom) // 0x4000
    os.makedirs(args.out, exist_ok=True)

    code_banks: set[int] = set()
    for part in args.code_banks.split(","):
        if "-" in part:
            a, b = part.split("-"); code_banks.update(range(int(a), int(b) + 1))
        elif part.strip():
            code_banks.add(int(part))

    print(f"ROM: {len(rom)} bytes, {nbanks} banks; {len(syms.symbols)} symbols")
    print(f"Translating banks as CODE: {sorted(code_banks)}; rest emitted as DATA.")

    for bank in range(nbanks):
        if bank in code_banks:
            blocks = disassemble_bank(rom, bank, syms)
            src = emit_bank(bank, blocks)
            print(f"  bank {bank:02x}: {len(blocks)} blocks")
        else:
            src = emit_data_bank(rom, bank)
        with open(os.path.join(args.out, f"bank_{bank:02x}.c"), "w") as fh:
            fh.write(src)

    print(f"Wrote {nbanks} bank files to {args.out}/")
    print("NOTE: incremental — broaden --code-banks as translate.py coverage grows.")


if __name__ == "__main__":
    main()
