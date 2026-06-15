// main.js — browser frontend. Loads the recompiled .wasm, blits its framebuffer to
// canvas, maps input, and drives gb_run_frame() once per animation frame.
// Deliberately thin: no game logic lives here (ARCHITECTURE.md §5).

const W = 160, H = 144;
const canvas = document.getElementById("screen");
const ctx = canvas.getContext("2d");
const image = ctx.createImageData(W, H);
const status = (msg) => { ctx.fillStyle = "#000"; ctx.fillRect(0, 0, W, H);
                          ctx.fillStyle = "#9c9"; ctx.font = "8px monospace";
                          msg.split("\n").forEach((l, i) => ctx.fillText(l, 4, 16 + i * 10)); };

// Our gb_set_buttons bit layout (gb.h): 0=A 1=B 2=Select 3=Start 4=R 5=L 6=U 7=D
const KEY = {
  "KeyZ": 0, "KeyX": 1, "ShiftLeft": 2, "ShiftRight": 2, "Enter": 3,
  "ArrowRight": 4, "ArrowLeft": 5, "ArrowUp": 6, "ArrowDown": 7,
};
let buttons = 0;
addEventListener("keydown", e => { if (e.code in KEY) { buttons |= 1 << KEY[e.code]; e.preventDefault(); } });
addEventListener("keyup",   e => { if (e.code in KEY) { buttons &= ~(1 << KEY[e.code]); e.preventDefault(); } });

async function fetchBytes(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: ${res.status}`);
  return new Uint8Array(await res.arrayBuffer());
}

async function main() {
  status("loading wasm...");
  let X;
  try {
    const { instance } = await WebAssembly.instantiateStreaming(fetch("./pokecrystal.wasm"), { env: {} });
    X = instance.exports;
  } catch (e) { status("build the .wasm first\n(see README)"); console.error(e); return; }
  const mem = X.memory;

  if (X.gb_boot) {
    // Self-contained build: the ROM data is embedded in the wasm. No file to load.
    X.gb_boot();
  } else {
    // Code-only build: fetch a locally-built ROM and hand it to gb_init.
    status("loading ROM...");
    let rom;
    try { rom = await fetchBytes("./pokecrystal.gbc"); }
    catch (e) { status("place a locally-built\npokecrystal.gbc next to\nindex.html (see README)"); console.error(e); return; }
    const romPtr = (X.__heap_base ? X.__heap_base.value : 0x20000);
    const need = romPtr + rom.length;
    if (need > mem.buffer.byteLength) mem.grow(Math.ceil((need - mem.buffer.byteLength) / 65536) + 1);
    new Uint8Array(mem.buffer).set(rom, romPtr);
    X.gb_init(romPtr, rom.length);
  }

  function frame() {
    X.gb_set_buttons(buttons);
    X.gb_run_frame();
    const fb = X.gb_framebuffer();
    image.data.set(new Uint8Array(mem.buffer).subarray(fb, fb + W * H * 4));
    ctx.putImageData(image, 0, 0);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

main();
