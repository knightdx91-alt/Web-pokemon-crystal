"""Parse rgbds .sym / .map output into a symbol table.

A .sym line looks like:   BB:AAAA Label        (BB = bank hex, AAAA = addr hex)
Comments start with ';'.  This is the recompiler's seed for separating code from
data and for naming call/jump targets (ARCHITECTURE.md §2-§3).
"""
from __future__ import annotations
from dataclasses import dataclass
import re

_SYM_RE = re.compile(r"^\s*([0-9A-Fa-f]{2,3}):([0-9A-Fa-f]{4})\s+(\S+)")


@dataclass(frozen=True)
class Symbol:
    bank: int
    addr: int          # 0x0000-0xFFFF as written in the bank window
    name: str

    @property
    def is_code_hint(self) -> bool:
        # rgbds data labels are conventionally suffixed; heuristic, refined by disasm.
        return not self.name.endswith(("Data", "GFX", "Pals", "Palettes", "Map"))


class SymbolTable:
    def __init__(self) -> None:
        self.symbols: list[Symbol] = []
        self._by_addr: dict[tuple[int, int], Symbol] = {}

    def add(self, sym: Symbol) -> None:
        self.symbols.append(sym)
        self._by_addr.setdefault((sym.bank, sym.addr), sym)

    def name_for(self, bank: int, addr: int) -> str | None:
        s = self._by_addr.get((bank, addr))
        return s.name if s else None

    def code_seeds(self) -> list[Symbol]:
        """Labels that plausibly start code, used to seed recursive disassembly."""
        return [s for s in self.symbols if s.is_code_hint]

    @classmethod
    def parse(cls, path: str) -> "SymbolTable":
        table = cls()
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.split(";", 1)[0]
                m = _SYM_RE.match(line)
                if not m:
                    continue
                bank = int(m.group(1), 16)
                addr = int(m.group(2), 16)
                table.add(Symbol(bank, addr, m.group(3)))
        return table


if __name__ == "__main__":  # quick manual check: python symfile.py path.sym
    import sys
    t = SymbolTable.parse(sys.argv[1])
    print(f"{len(t.symbols)} symbols, {len(t.code_seeds())} code seeds")
