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
const DIR_MASK = (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7); // R L U D
addEventListener("keydown", e => { if (e.code in KEY) { buttons |= 1 << KEY[e.code]; e.preventDefault(); } });
addEventListener("keyup",   e => { if (e.code in KEY) { buttons &= ~(1 << KEY[e.code]); e.preventDefault(); } });

// --- On-screen A/B/Start/Select buttons (touch + mouse) ---------------------
for (const el of document.querySelectorAll(".btn[data-btn]")) {
  const bit = 1 << Number(el.dataset.btn);
  const press = e => { buttons |= bit; el.classList.add("held"); e.preventDefault(); };
  const release = e => { buttons &= ~bit; el.classList.remove("held"); e.preventDefault(); };
  el.addEventListener("pointerdown", press);
  el.addEventListener("pointerup", release);
  el.addEventListener("pointercancel", release);
  el.addEventListener("pointerleave", release);
}

// --- Drag-to-move on the game area: drag direction = held D-pad --------------
// Touch (or mouse-drag) the screen and pull in a direction; that direction is
// held like a D-pad until you release. Diagonals allowed. Small dead-zone.
(function dragToMove() {
  const screen = document.getElementById("screen");
  let active = null, ox = 0, oy = 0;
  const DEAD = 14; // px before movement registers
  const update = (x, y) => {
    let dir = 0;
    const dx = x - ox, dy = y - oy;
    if (Math.hypot(dx, dy) >= DEAD) {
      if (dx >  DEAD * 0.5) dir |= 1 << 4;      // Right
      if (dx < -DEAD * 0.5) dir |= 1 << 5;      // Left
      if (dy < -DEAD * 0.5) dir |= 1 << 6;      // Up
      if (dy >  DEAD * 0.5) dir |= 1 << 7;      // Down
    }
    buttons = (buttons & ~DIR_MASK) | dir;
  };
  screen.addEventListener("pointerdown", e => { active = e.pointerId; ox = e.clientX; oy = e.clientY;
                                                screen.setPointerCapture(e.pointerId); e.preventDefault(); });
  screen.addEventListener("pointermove", e => { if (e.pointerId === active) { update(e.clientX, e.clientY); e.preventDefault(); } });
  const end = e => { if (e.pointerId === active) { active = null; buttons &= ~DIR_MASK; e.preventDefault(); } };
  screen.addEventListener("pointerup", end);
  screen.addEventListener("pointercancel", end);
})();

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

  setupAudio(X, mem);

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

// --- Audio: drain the wasm PCM ring into WebAudio (resampled) ---------------
function setupAudio(X, mem) {
  if (!X.gb_audio_samples || !window.AudioContext && !window.webkitAudioContext) return;
  const RINGF = 16384, OUT_HZ = 32768;
  const actx = new (window.AudioContext || window.webkitAudioContext)();
  const ratio = OUT_HZ / actx.sampleRate;     // wasm frames per output frame
  const node = actx.createScriptProcessor(2048, 0, 2);
  let frac = 0;
  node.onaudioprocess = (e) => {
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
    const ring = new Int16Array(mem.buffer, X.gb_audio_samples(), RINGF * 2);
    const avail = X.gb_audio_available();
    const tail = X.gb_audio_tail();
    let pos = frac, consumed = 0;
    for (let i = 0; i < L.length; i++) {
      const idx = pos | 0;
      if (idx < avail) {
        const f = (tail + idx) & (RINGF - 1);
        L[i] = ring[f * 2] / 32768; R[i] = ring[f * 2 + 1] / 32768;
      } else { L[i] = 0; R[i] = 0; }   // underrun -> silence
      pos += ratio;
    }
    consumed = Math.min(avail, pos | 0);
    frac = pos - consumed;
    X.gb_audio_consume(consumed);
  };
  node.connect(actx.destination);
  // Browsers block audio until a user gesture — resume on first input.
  const resume = () => actx.resume();
  addEventListener("pointerdown", resume, { once: true });
  addEventListener("keydown", resume, { once: true });
}

main();
