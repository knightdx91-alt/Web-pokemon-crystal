// Boot the real ROM in the recompiled wasm, run to first trap, and dump the
// execution trace annotated with nearest symbols. Usage: node tools/trace_boot.mjs
import fs from "fs";
const ROM = "build/rom/pokecrystal.gbc";
const SYM = "build/rom/pokecrystal.sym";
const WASM = "web/pokecrystal.wasm";

// load symbols: bank -> sorted [addr,name]
const syms = new Map();
for (const line of fs.readFileSync(SYM, "utf8").split("\n")) {
  const m = line.match(/^([0-9A-Fa-f]{2,3}):([0-9A-Fa-f]{4})\s+(\S+)/);
  if (!m) continue;
  const b = parseInt(m[1], 16), a = parseInt(m[2], 16);
  if (!syms.has(b)) syms.set(b, []);
  syms.get(b).push([a, m[3]]);
}
for (const arr of syms.values()) arr.sort((x, y) => x[0] - y[0]);
function nearest(bank, addr) {
  const arr = syms.get(bank) || [];
  let lo = 0, hi = arr.length - 1, best = null;
  while (lo <= hi) { const mid = (lo + hi) >> 1;
    if (arr[mid][0] <= addr) { best = arr[mid]; lo = mid + 1; } else hi = mid - 1; }
  if (!best) return "?";
  const off = addr - best[0];
  return best[1] + (off ? `+${off}` : "");
}

const rom = fs.readFileSync(ROM);
const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {});
const ex = instance.exports;
let mem = new Uint8Array(ex.memory.buffer);
const ptr = (ex.__heap_base && ex.__heap_base.value) || 0x10000;
if (ptr + rom.length > mem.length) ex.memory.grow(Math.ceil((ptr + rom.length - mem.length) / 65536) + 1);
new Uint8Array(ex.memory.buffer).set(rom, ptr);
ex.gb_init(ptr, rom.length);

function snapshot() {                 // copy the last 30 ring entries this frame
  const head = ex.gb_dbg_trace_head(), out = [];
  for (let k = 30; k >= 1; k--) out.push(ex.gb_dbg_trace_at(head - k));
  return out;
}
let prev = [], trapFrame = -1;
for (let i = 0; i < 600; i++) {
  const before = snapshot();
  ex.gb_run_frame();
  if (ex.gb_dbg_traps() > 0) { trapFrame = i; prev = before; break; }
}
const tb = ex.gb_dbg_trap_bank(), tp = ex.gb_dbg_trap_pc();
console.log(`first trap at frame ${trapFrame}, addr ${tb.toString(16)}:${tp.toString(16)} = ${nearest(tb, tp)}`);
console.log("last 30 blocks dispatched in the frame BEFORE the trap:");
for (const v of prev) {
  const b = v >>> 16, a = v & 0xffff;
  console.log(`  ${b.toString(16)}:${a.toString(16)}  ${nearest(b, a)}`);
}
