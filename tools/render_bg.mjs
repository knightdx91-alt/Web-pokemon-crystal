// Render the BG tilemap+tiles to a grayscale PNG (ignoring CGB palettes), to see
// the actual graphics content. Usage: node tools/render_bg.mjs [frames] [out.png]
import fs from "fs"; import zlib from "zlib";
const FRAMES = parseInt(process.argv[2] || "1500");
const OUT = process.argv[3] || "/tmp/bg.png";
const rom = fs.readFileSync("build/rom/pokecrystal.gbc");
const { instance } = await WebAssembly.instantiate(fs.readFileSync("web/pokecrystal.wasm"), {});
const ex = instance.exports;
let mem = new Uint8Array(ex.memory.buffer);
const ptr = (ex.__heap_base && ex.__heap_base.value) || 0x10000;
if (ptr + rom.length > mem.length) ex.memory.grow(Math.ceil((ptr + rom.length - mem.length) / 65536) + 1);
new Uint8Array(ex.memory.buffer).set(rom, ptr);
ex.gb_init(ptr, rom.length);
for (let i = 0; i < FRAMES; i++) ex.gb_run_frame();
mem = new Uint8Array(ex.memory.buffer);
const io = a => ex.gb_dbg_io(a), vbase = ex.gb_dbg_vram();
const lcdc = io(0x40), scx = io(0x43), scy = io(0x42);
const mapBase = (lcdc & 0x08) ? 0x1c00 : 0x1800, signed = !(lcdc & 0x10);
const vr = (b, off) => mem[vbase + b * 0x2000 + off];
const W = 160, H = 144, img = Buffer.alloc(W * H), shade = [255, 170, 85, 0];
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  const mx = (x + scx) & 0xff, my = (y + scy) & 0xff;
  const ent = mapBase + (my >> 3) * 32 + (mx >> 3);
  const tile = vr(0, ent), attr = vr(1, ent), tbank = (attr >> 3) & 1;
  const taddr = signed ? (0x1000 + ((tile << 24 >> 24) * 16)) : (tile * 16);
  const lo = vr(tbank, taddr + (my & 7) * 2), hi = vr(tbank, taddr + (my & 7) * 2 + 1);
  const bit = 7 - (mx & 7), cid = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
  img[y * W + x] = shade[cid];
}
function png(w, h, g) {
  const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const tt = Buffer.from(t), c = Buffer.alloc(4);
    let v = ~0; const b = Buffer.concat([tt, d]); for (let i = 0; i < b.length; i++) { v ^= b[i]; for (let k = 0; k < 8; k++) v = (v >>> 1) ^ (0xEDB88320 & -(v & 1)); } c.writeUInt32BE(~v >>> 0); return Buffer.concat([l, tt, d, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 0;
  const raw = Buffer.alloc((w + 1) * h); for (let y = 0; y < h; y++) g.copy(raw, y * (w + 1) + 1, y * w, y * w + w);
  return Buffer.concat([sig, chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}
fs.writeFileSync(OUT, png(W, H, img));
console.log(`wrote ${OUT} @frame ${FRAMES} (LCDC=${lcdc.toString(16)} scx=${scx} scy=${scy})`);
