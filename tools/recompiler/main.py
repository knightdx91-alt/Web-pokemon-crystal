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


def emit_dispatch(code_banks: set[int], nbanks: int) -> str:
    """Generate the bank function-pointer table + rom_dispatch trampoline.

    rom_dispatch routes cpu.pc to the correct bank function: home ($0000-$3FFF)
    always goes to bank 0; the switchable window ($4000-$7FFF) goes to the bank
    selected by cpu.rom_bank. Untranslated banks route to a trap stub.
    """
    decls = "\n".join(f"void bank_{b:02x}(uint16_t pc);" for b in sorted(code_banks))
    table = ",\n    ".join(
        (f"bank_{b:02x}" if b in code_banks else "bank_stub") for b in range(nbanks))
    return f"""/* dispatch.c — generated bank trampoline (do not edit) */
#include "gb.h"
#include "hal_internal.h"

{decls}

static void bank_stub(uint16_t pc) {{
    /* execution entered an untranslated bank: stop cleanly (see ROADMAP Phase 3) */
    trap_pc(pc);
    cpu.halted = 1;
}}

static void (*const bank_table[{nbanks}])(uint16_t) = {{
    {table}
}};

void rom_dispatch(uint16_t pc) {{
    if (pc >= 0xFF80 && pc < 0xFFFF) {{ hram_exec(pc); return; }}  /* HRAM (OAM DMA) */
    if (pc < 0x4000) bank_00(pc);                 /* home bank, always mapped */
    else {{
        uint8_t b = cpu.rom_bank;
        if (b < {nbanks}) bank_table[b](pc);
        else trap_pc(pc);
    }}
}}
"""


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

    code_banks: set[int] = {0}            # home bank is always required
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

    with open(os.path.join(args.out, "dispatch.c"), "w") as fh:
        fh.write(emit_dispatch(code_banks, nbanks))

    print(f"Wrote {written} code-bank file(s) + dispatch.c to {args.out}/ "
          f"(data stays in g_rom, provided at runtime).")
    print("NOTE: incremental — broaden --code-banks as coverage/data-separation grows.")


if __name__ == "__main__":
    main()
