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


# NOTE: ROM bytes are provided to the runtime at load time via gb_init(rom,len) and
# read through bus_read()/g_rom, so we deliberately do NOT emit data banks as C
# arrays — that would duplicate the entire ROM inside the .wasm. Only executable
# banks become C (dispatch functions); everything else stays data in g_rom.


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
    print(f"Translating banks as CODE: {sorted(code_banks)}; ROM data served from g_rom.")

    written = 0
    for bank in sorted(code_banks):
        if bank >= nbanks:
            continue
        blocks = disassemble_bank(rom, bank, syms)
        src = emit_bank(bank, blocks)
        with open(os.path.join(args.out, f"bank_{bank:02x}.c"), "w") as fh:
            fh.write(src)
        written += 1
        print(f"  bank {bank:02x}: {len(blocks)} blocks")

    print(f"Wrote {written} code-bank file(s) to {args.out}/ "
          f"(data stays in g_rom, provided at runtime).")
    print("NOTE: incremental — broaden --code-banks as coverage/data-separation grows.")


if __name__ == "__main__":
    main()
