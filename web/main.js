// main.js — browser frontend. Loads the recompiled .wasm, blits its framebuffer to
// canvas, maps input, and drives gb_run_frame() once per animation frame.
// Deliberately thin: no game logic lives here (ARCHITECTURE.md §5).

const W = 160, H = 144;
const canvas = document.getElementById("screen");
const ctx = canvas.getContext("2d");
const image = ctx.createImageData(W, H);

// bit layout matches gb_set_buttons (gb.h)
const KEY = {
  "KeyZ": 0, "KeyX": 1, "ShiftLeft": 2, "Enter": 3,
  "ArrowRight": 4, "ArrowLeft": 5, "ArrowUp": 6, "ArrowDown": 7,
};
let buttons = 0;
addEventListener("keydown", e => { if (e.code in KEY) { buttons |= 1 << KEY[e.code]; e.preventDefault(); } });
addEventListener("keyup",   e => { if (e.code in KEY) { buttons &= ~(1 << KEY[e.code]); e.preventDefault(); } });

async function loadRom() {
  // The built ROM is produced locally (see README legal note) and not committed.
  const res = await fetch("./pokecrystal.gbc");
  if (!res.ok) throw new Error("place a locally-built pokecrystal.gbc next to index.html");
  return new Uint8Array(await res.arrayBuffer());
}

async function main() {
  let wasm;
  try {
    wasm = await WebAssembly.instantiateStreaming(fetch("./pokecrystal.wasm"), {
      env: { /* imported syscalls, if any, wired here */ },
    });
  } catch (e) {
    ctx.fillStyle = "#fff"; ctx.fillText("build the .wasm first (see README)", 6, 72);
    console.error(e); return;
  }
  const X = wasm.instance.exports;
  const mem = () => new Uint8Array(X.memory.buffer);

  const rom = await loadRom();
  const romPtr = X.malloc ? X.malloc(rom.length) : 0;   // TODO: allocator export
  mem().set(rom, romPtr);
  X.gb_init(romPtr, rom.length);

  function frame() {
    X.gb_set_buttons(buttons);
    X.gb_run_frame();
    const fb = X.gb_framebuffer();
    image.data.set(mem().subarray(fb, fb + W * H * 4));
    ctx.putImageData(image, 0, 0);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

main();
