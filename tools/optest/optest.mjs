// SingleStepTests (sm83) driver: validates our instruction semantics.
// Usage:
//   node tools/optest/optest.mjs            # all 256 base + 256 CB opcodes
//   node tools/optest/optest.mjs 80 06 cb11 # specific opcodes
// Tests are fetched from the SingleStepTests/sm83 repo and cached in /tmp/sm83.
import fs from "fs";
import path from "path";

const CACHE = "/tmp/sm83";
const BASE = "https://raw.githubusercontent.com/SingleStepTests/sm83/main/v1";
fs.mkdirSync(CACHE, { recursive: true });

async function loadTests(op) {            // op like "80" or "cb 11"
  const file = op + ".json";
  const local = path.join(CACHE, file.replace(" ", "_"));
  if (fs.existsSync(local)) return JSON.parse(fs.readFileSync(local, "utf8"));
  const url = `${BASE}/${encodeURIComponent(file)}`;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`fetch ${url} -> ${res.status}`);
  const txt = await res.text();
  fs.writeFileSync(local, txt);
  return JSON.parse(txt);
}

const wasm = await WebAssembly.instantiate(fs.readFileSync("tools/optest/optest.wasm"), {});
const X = wasm.instance.exports;

function runOne(t) {
  const i = t.initial;
  X.set_state(i.a, i.f, i.b, i.c, i.d, i.e, i.h, i.l, i.sp, i.pc);
  for (const [addr, val] of i.ram) X.wr(addr, val);
  X.step();
  const F = t.final;
  const got = { a: X.g_a(), f: X.g_f(), b: X.g_b(), c: X.g_c(), d: X.g_d(),
                e: X.g_e(), h: X.g_h(), l: X.g_l(), sp: X.g_sp(), pc: X.g_pc() };
  const diffs = [];
  for (const k of ["a", "f", "b", "c", "d", "e", "h", "l", "sp", "pc"])
    if (got[k] !== F[k]) diffs.push(`${k}: got ${got[k]} want ${F[k]}`);
  for (const [addr, val] of F.ram) {
    const g = X.rd(addr);
    if (g !== val) diffs.push(`ram[${addr}]: got ${g} want ${val}`);
  }
  return diffs;
}

function opList() {
  const args = process.argv.slice(2);
  if (args.length) return args.map(a => a.replace(/^cb/, "cb "));
  const ops = [];
  for (let o = 0; o < 256; o++) if (o !== 0xcb) ops.push(o.toString(16).padStart(2, "0"));
  for (let c = 0; c < 256; c++) ops.push("cb " + c.toString(16).padStart(2, "0"));
  return ops;
}

let totalFail = 0, opsFail = 0, opsRun = 0, missing = 0;
for (const op of opList()) {
  let tests;
  try { tests = await loadTests(op); }
  catch (e) { missing++; continue; }      // undefined opcodes have no test file
  opsRun++;
  let fails = 0, sample = null;
  for (const t of tests) {
    const d = runOne(t);
    if (d.length) { fails++; if (!sample) sample = `${t.name}: ${d.join(", ")}`; }
  }
  if (fails) {
    opsFail++; totalFail += fails;
    console.log(`FAIL ${op}: ${fails}/${tests.length}   e.g. ${sample}`);
  }
}
console.log(`\nopcodes run ${opsRun}, no test file ${missing}, opcodes failing ${opsFail}, total failing tests ${totalFail}`);
process.exit(opsFail ? 1 : 0);
