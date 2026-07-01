import { Nes, initWasm, setPaletteEntry } from "./nes-wasm.js";
import { CrtRenderer } from "./crt.js";
import { RomLibrary } from "./lib.js";

await initWasm();

// ── DOM refs ──────────────────────────────────────────────────────────────
const canvas       = document.getElementById("screen")         as HTMLCanvasElement;
const screenWrap   = document.getElementById("screen-wrap")    as HTMLDivElement;
const romInput     = document.getElementById("rom-input")      as HTMLInputElement;
const btnPause     = document.getElementById("btn-pause")      as HTMLButtonElement;
const btnReset     = document.getElementById("btn-reset")      as HTMLButtonElement;
const btnSave      = document.getElementById("btn-save")       as HTMLButtonElement;
const btnLoadSt    = document.getElementById("btn-load-state") as HTMLButtonElement;
const btnMenu      = document.getElementById("btn-menu")       as HTMLButtonElement;
const btnClose     = document.getElementById("btn-close")      as HTMLButtonElement;
const btnCrt       = document.getElementById("btn-crt")        as HTMLButtonElement;
const btnFull      = document.getElementById("btn-fullscreen") as HTMLButtonElement;
const btnShot      = document.getElementById("btn-screenshot") as HTMLButtonElement;
const btnResetPal  = document.getElementById("btn-reset-pal")  as HTMLButtonElement;
const overlay      = document.getElementById("overlay")        as HTMLDivElement;
const settingsEl   = document.getElementById("settings")       as HTMLDivElement;
const fpsEl        = document.getElementById("fps")            as HTMLSpanElement;
const statusEl     = document.getElementById("status")         as HTMLSpanElement;
const volumeSlider = document.getElementById("volume")         as HTMLInputElement;
const libraryGrid  = document.getElementById("library-grid")   as HTMLDivElement;
const libraryEmpty = document.getElementById("library-empty")  as HTMLParagraphElement;

// ── CRT renderer ──────────────────────────────────────────────────────────
const crt = new CrtRenderer(canvas);

// ── ROM Library ───────────────────────────────────────────────────────────
const library = new RomLibrary();
await library.open();

// ── State ─────────────────────────────────────────────────────────────────
let nes: Nes | null = null;
let paused     = false;
let romName    = "";
let romHash    = "";
let wizardHash = "";
let speed      = 1;

// ── Audio ─────────────────────────────────────────────────────────────────
let audioCtx:    AudioContext     | null = null;
let workletNode: AudioWorkletNode | null = null;
let gainNode:    GainNode         | null = null;
let audioReady = false;

async function initAudio(): Promise<void> {
  if (audioCtx) return;
  try {
    audioCtx    = new AudioContext();
    await audioCtx.audioWorklet.addModule("/apu-worklet.js");
    workletNode = new AudioWorkletNode(audioCtx, "apu-processor");
    gainNode    = audioCtx.createGain();
    gainNode.gain.value = Number(volumeSlider.value);
    workletNode.connect(gainNode).connect(audioCtx.destination);
    nes?.apu.setSampleRate(audioCtx.sampleRate);
    audioReady = true;
  } catch (err) {
    console.warn("Audio init failed:", err);
  }
}

function resumeAudioOnGesture(): void {
  const go = () => void initAudio().then(() => audioCtx?.resume());
  window.addEventListener("pointerdown", go, { once: true });
  window.addEventListener("keydown",     go, { once: true });
}
resumeAudioOnGesture();

const audioBuf = new Float32Array(4096);
function pumpAudio(): void {
  if (!nes || !audioReady || !workletNode) return;
  const got = nes.apu.drainSamples(audioBuf);
  if (got > 0) {
    const copy = audioBuf.slice(0, got);
    workletNode.port.postMessage(copy, [copy.buffer]);
  }
}

volumeSlider.addEventListener("input", () => {
  if (gainNode) gainNode.gain.value = Number(volumeSlider.value);
});

// ── Rendering ─────────────────────────────────────────────────────────────
const FRAME_MS  = 1000 / 60.0988;
let lastTime    = 0;
let accumulator = 0;
let fpsFrames   = 0;
let fpsLast     = 0;

function blit(): void {
  if (!nes) return;
  const fb = nes.framebuffer;
  crt.upload(new Uint8Array(fb.buffer, fb.byteOffset, fb.byteLength));
  crt.render();
}

// ── Speed control ─────────────────────────────────────────────────────────
document.querySelectorAll<HTMLButtonElement>(".speed-btn").forEach(btn => {
  btn.addEventListener("click", () => {
    speed = Number(btn.dataset.speed);
    document.querySelectorAll(".speed-btn").forEach(b => b.classList.remove("active"));
    btn.classList.add("active");
  });
});

// ── Input ─────────────────────────────────────────────────────────────────
const enum Btn { A=0,B=1,Select=2,Start=3,Up=4,Down=5,Left=6,Right=7 }
const KEYMAP: Record<string, Btn> = {
  ArrowUp: Btn.Up, ArrowDown: Btn.Down, ArrowLeft: Btn.Left, ArrowRight: Btn.Right,
  KeyZ: Btn.B, KeyX: Btn.A, Enter: Btn.Start, ShiftLeft: Btn.Select, ShiftRight: Btn.Select,
};

window.addEventListener("keydown", (e) => {
  if (e.code === "F5")  { saveState(); e.preventDefault(); return; }
  if (e.code === "F8")  { loadState(); e.preventDefault(); return; }
  const btn = KEYMAP[e.code];
  if (btn !== undefined && nes) { nes.controller1.setButton(btn, true); e.preventDefault(); }
});
window.addEventListener("keyup", (e) => {
  const btn = KEYMAP[e.code];
  if (btn !== undefined && nes) nes.controller1.setButton(btn, false);
});

// ── Gamepad ───────────────────────────────────────────────────────────────
const GP_MAP: Record<number, Btn> = { 0:Btn.A, 1:Btn.B, 8:Btn.Select, 9:Btn.Start,
  12:Btn.Up, 13:Btn.Down, 14:Btn.Left, 15:Btn.Right };
const gpState: Record<number, boolean> = {};

function pollGamepad(): void {
  const gps = navigator.getGamepads();
  const gp  = gps[0]; if (!gp || !nes) return;
  for (const [idx, btn] of Object.entries(GP_MAP)) {
    const pressed = gp.buttons[Number(idx)]?.pressed ?? false;
    if (pressed !== gpState[Number(idx)]) {
      gpState[Number(idx)] = pressed;
      nes.controller1.setButton(btn, pressed);
    }
  }
  // D-pad via axes if no digital buttons
  const ax = gp.axes;
  if (ax.length >= 2) {
    nes.controller1.setButton(Btn.Left,  ax[0] < -0.5);
    nes.controller1.setButton(Btn.Right, ax[0] >  0.5);
    nes.controller1.setButton(Btn.Up,    ax[1] < -0.5);
    nes.controller1.setButton(Btn.Down,  ax[1] >  0.5);
  }
}

// ── ROM loading ───────────────────────────────────────────────────────────
function hashRom(data: Uint8Array): string {
  let h = 0x811c9dc5;
  for (let i = 0; i < data.length; i++) { h ^= data[i]; h = Math.imul(h, 0x01000193); }
  return (h >>> 0).toString(16).padStart(8, "0");
}

function captureThumbnail(): string {
  // Run a few frames to get past the splash, then snapshot
  if (!nes) return "";
  for (let i = 0; i < 10; i++) nes.stepFrame();
  blit();
  return canvas.toDataURL("image/png");
}

async function loadRom(data: Uint8Array, name: string, skipLibrary = false): Promise<void> {
  try { nes = new Nes(data); }
  catch (err) {
    setStatus(`Failed to load ${name}: ${err instanceof Error ? err.message : err}`);
    return;
  }
  romHash = hashRom(data);
  romName = name.replace(/\.nes$/i, "");
  paused  = false;
  accumulator = 0; lastTime = 0;

  void initAudio().then(() => {
    audioCtx?.resume();
    if (audioCtx) nes?.apu.setSampleRate(audioCtx.sampleRate);
  });

  btnPause.disabled = btnReset.disabled = btnSave.disabled = btnLoadSt.disabled = false;
  btnPause.textContent = "⏸ PAUSE";
  setStatus(`▶ ${romName}`);
  canvas.focus();

  // Save to library (async, don't block)
  if (!skipLibrary) {
    const alreadySaved = await library.has(romHash);
    if (!alreadySaved) {
      const thumb = captureThumbnail();
      await library.save(romHash, romName, nes.cart.mapperId, data, thumb);
    } else {
      await library.updateLastPlayed(romHash);
    }
    renderLibrary();
  }
}

romInput.addEventListener("change", async () => {
  const file = romInput.files?.[0];
  if (!file) return;
  await loadRom(new Uint8Array(await file.arrayBuffer()), file.name);
  romInput.value = "";
});
window.addEventListener("dragover", (e) => e.preventDefault());
window.addEventListener("drop", async (e) => {
  e.preventDefault();
  const file = e.dataTransfer?.files[0];
  if (file) await loadRom(new Uint8Array(await file.arrayBuffer()), file.name);
});

// ── ROM Library UI ────────────────────────────────────────────────────────
function makeLibCard(e: { hash: string; name: string; thumbnail: string }, featured: boolean): HTMLDivElement {
  const card = document.createElement("div");
  if (featured) {
    card.className = "lib-card featured";
    card.innerHTML = `
      <img src="${e.thumbnail}" alt="${e.name}">
      <div class="lib-card-info">
        <div class="lib-card-badge">★ FEATURED</div>
        <div class="lib-card-title">${e.name.toUpperCase()}</div>
        <div class="lib-card-sub">NES HOMEBREW</div>
      </div>`;
  } else {
    card.className = "lib-card";
    card.innerHTML = `
      <img src="${e.thumbnail}" alt="${e.name}">
      <div class="lib-card-name">${e.name}</div>
      <button class="lib-card-del" title="Remove">✕</button>`;
    card.querySelector(".lib-card-del")!.addEventListener("click", async (ev) => {
      ev.stopPropagation();
      await library.remove(e.hash);
      void renderLibrary();
    });
  }
  card.querySelector("img")!.addEventListener("click", async () => {
    const data = await library.getData(e.hash);
    if (data) { await loadRom(data, e.name + ".nes", true); await library.updateLastPlayed(e.hash); }
  });
  return card;
}

async function renderLibrary(): Promise<void> {
  const entries = await library.getAll();
  libraryEmpty.style.display = entries.length ? "none" : "block";
  libraryGrid.innerHTML = "";
  const featured = entries.find(e => e.hash === wizardHash);
  if (featured) libraryGrid.appendChild(makeLibCard(featured, true));
  for (const e of entries) {
    if (e.hash !== wizardHash) libraryGrid.appendChild(makeLibCard(e, false));
  }
}

async function seedWizard(): Promise<void> {
  try {
    const res = await fetch("/wizard.nes");
    if (!res.ok) return;
    const data = new Uint8Array(await res.arrayBuffer());
    wizardHash = hashRom(data);
    if (!await library.has(wizardHash)) {
      const tmpNes = new Nes(data);
      // Boot to title, press Start, wait for gameplay
      for (let i = 0; i < 30; i++) tmpNes.stepFrame();
      tmpNes.controller1.setButton(3, true);   // Start down
      for (let i = 0; i < 5; i++) tmpNes.stepFrame();
      tmpNes.controller1.setButton(3, false);  // Start up
      for (let i = 0; i < 150; i++) tmpNes.stepFrame();
      const fb = tmpNes.framebuffer;
      const offscreen = document.createElement("canvas");
      offscreen.width = 256; offscreen.height = 240;
      const ctx2d = offscreen.getContext("2d")!;
      const rgba = new Uint8ClampedArray(fb.buffer, fb.byteOffset, 256 * 240 * 4);
      ctx2d.putImageData(new ImageData(rgba, 256, 240), 0, 0);
      const thumb = offscreen.toDataURL("image/png");
      tmpNes.destroy();
      await library.save(wizardHash, "Wizard's Stand", 0, data, thumb);
    }
  } catch (err) {
    console.warn("Could not seed wizard.nes:", err);
  }
  void renderLibrary();
}

void renderLibrary();
void seedWizard();

// ── Save states ───────────────────────────────────────────────────────────
function saveState(): void {
  if (!nes || !romHash) return;
  try { localStorage.setItem(`petnes-state-${romHash}`, nes.serialize()); setStatus("💾 State saved"); }
  catch (e) { setStatus(`Save failed: ${e}`); }
}
function loadState(): void {
  if (!nes || !romHash) return;
  const s = localStorage.getItem(`petnes-state-${romHash}`);
  if (!s) { setStatus("No saved state"); return; }
  try { nes.deserialize(s); setStatus("📂 State loaded"); }
  catch (e) { setStatus(`Load failed: ${e}`); }
}
function setStatus(t: string): void { statusEl.textContent = t; }

// ── Toolbar ───────────────────────────────────────────────────────────────
btnPause.addEventListener("click", () => {
  paused = !paused;
  btnPause.textContent = paused ? "▶ RESUME" : "⏸ PAUSE";
  setStatus(paused ? `⏸ ${romName}` : `▶ ${romName}`);
  if (!paused) { lastTime = 0; accumulator = 0; }
  canvas.focus();
});
btnReset.addEventListener("click",    () => { nes?.reset(); canvas.focus(); });
btnSave.addEventListener("click",     () => { saveState(); canvas.focus(); });
btnLoadSt.addEventListener("click",   () => { loadState(); canvas.focus(); });

// ── Settings panel ────────────────────────────────────────────────────────
function openSettings():  void { settingsEl.classList.add("open"); overlay.classList.add("open"); }
function closeSettings(): void { settingsEl.classList.remove("open"); overlay.classList.remove("open"); }
btnMenu.addEventListener("click",  openSettings);
btnClose.addEventListener("click", closeSettings);
overlay.addEventListener("click",  closeSettings);

btnCrt.addEventListener("click", () => {
  crt.crtEnabled = !crt.crtEnabled;
  btnCrt.classList.toggle("active", crt.crtEnabled);
});

btnFull.addEventListener("click", () => {
  if (!document.fullscreenElement) screenWrap.requestFullscreen();
  else document.exitFullscreen();
});
document.addEventListener("fullscreenchange", () => {
  if (document.fullscreenElement === screenWrap) {
    requestAnimationFrame(() => {
      const vw = window.innerWidth, vh = window.innerHeight;
      const scale = Math.min(vw / 768, vh / 720);
      const w = Math.floor(768 * scale), h = Math.floor(720 * scale);
      canvas.style.width      = w + "px";
      canvas.style.height     = h + "px";
      canvas.style.marginLeft = Math.floor((vw - w) / 2) + "px";
      canvas.style.marginTop  = Math.floor((vh - h) / 2) + "px";
    });
  } else {
    canvas.style.width = canvas.style.height = "";
    canvas.style.marginLeft = canvas.style.marginTop = "";
  }
});

btnShot.addEventListener("click", () => {
  blit();
  canvas.toBlob(blob => {
    if (!blob) return;
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url; a.download = `petnes-${romName || "screenshot"}.png`;
    a.click(); URL.revokeObjectURL(url);
  }, "image/png");
});

document.querySelectorAll<HTMLButtonElement>(".ch-btn").forEach(btn => {
  btn.addEventListener("click", () => {
    const ch    = Number(btn.dataset.ch);
    const muted = !btn.classList.toggle("active");
    nes?.apu.setChannelMute(ch, muted);
  });
});


// ── Palette ───────────────────────────────────────────────────────────────
const DEFAULT_PALETTE: [number,number,number][] = [
  [0x66,0x66,0x66],[0x00,0x2a,0x88],[0x14,0x12,0xa7],[0x3b,0x00,0xa4],
  [0x5c,0x00,0x7e],[0x6e,0x00,0x40],[0x6c,0x06,0x00],[0x56,0x1d,0x00],
  [0x33,0x35,0x00],[0x0b,0x48,0x00],[0x00,0x52,0x00],[0x00,0x4f,0x08],
  [0x00,0x40,0x4d],[0x00,0x00,0x00],[0x00,0x00,0x00],[0x00,0x00,0x00],
  [0xad,0xad,0xad],[0x15,0x5f,0xd9],[0x42,0x40,0xff],[0x75,0x27,0xfe],
  [0xa0,0x1a,0xcc],[0xb7,0x1e,0x7b],[0xb5,0x31,0x20],[0x99,0x4e,0x00],
  [0x6b,0x6d,0x00],[0x38,0x87,0x00],[0x0c,0x93,0x00],[0x00,0x8f,0x32],
  [0x00,0x7c,0x8d],[0x00,0x00,0x00],[0x00,0x00,0x00],[0x00,0x00,0x00],
  [0xff,0xfe,0xff],[0x64,0xb0,0xff],[0x92,0x90,0xff],[0xc6,0x76,0xff],
  [0xf3,0x6a,0xff],[0xfe,0x6e,0xcc],[0xfe,0x81,0x70],[0xea,0x9e,0x22],
  [0xbc,0xbe,0x00],[0x88,0xd8,0x00],[0x5c,0xe4,0x30],[0x45,0xe0,0x82],
  [0x48,0xcd,0xde],[0x4f,0x4f,0x4f],[0x00,0x00,0x00],[0x00,0x00,0x00],
  [0xff,0xfe,0xff],[0xc0,0xdf,0xff],[0xd3,0xd2,0xff],[0xe8,0xc8,0xff],
  [0xfb,0xc2,0xff],[0xfe,0xc4,0xea],[0xfe,0xcc,0xc5],[0xf7,0xd8,0xa5],
  [0xe4,0xe5,0x94],[0xcf,0xef,0x96],[0xbd,0xf4,0xab],[0xb3,0xf3,0xcc],
  [0xb5,0xeb,0xf2],[0xb8,0xb8,0xb8],[0x00,0x00,0x00],[0x00,0x00,0x00],
];
const currentPalette = DEFAULT_PALETTE.map(c => [...c] as [number,number,number]);

function buildPaletteGrid(): void {
  const grid = document.getElementById("palette-grid")!;
  grid.innerHTML = "";
  currentPalette.forEach(([r,g,b], idx) => {
    const sw = document.createElement("div");
    sw.className = "pal-swatch";
    sw.style.background = `rgb(${r},${g},${b})`;
    sw.addEventListener("click", () => {
      const picker = document.createElement("input");
      picker.type  = "color";
      picker.value = `#${r.toString(16).padStart(2,"0")}${g.toString(16).padStart(2,"0")}${b.toString(16).padStart(2,"0")}`;
      picker.style.cssText = "position:absolute;opacity:0;pointer-events:none";
      document.body.appendChild(picker);
      picker.click();
      picker.addEventListener("input", () => {
        const h = picker.value;
        const nr = parseInt(h.slice(1,3),16), ng = parseInt(h.slice(3,5),16), nb = parseInt(h.slice(5,7),16);
        currentPalette[idx] = [nr,ng,nb];
        sw.style.background = `rgb(${nr},${ng},${nb})`;
        setPaletteEntry(idx, nr, ng, nb);
      });
      picker.addEventListener("change", () => document.body.removeChild(picker));
    });
    grid.appendChild(sw);
  });
}
buildPaletteGrid();

function rgbToHsl(r: number, g: number, b: number): [number,number,number] {
  const rr=r/255,gg=g/255,bb=b/255;
  const max=Math.max(rr,gg,bb),min=Math.min(rr,gg,bb),l=(max+min)/2;
  if (max===min) return [0,0,l];
  const d=max-min, s=l>0.5?d/(2-max-min):d/(max+min);
  let h=0;
  if      (max===rr) h=((gg-bb)/d+(gg<bb?6:0))/6;
  else if (max===gg) h=((bb-rr)/d+2)/6;
  else               h=((rr-gg)/d+4)/6;
  return [h,s,l];
}
function hslToRgb(h:number,s:number,l:number):[number,number,number] {
  if (s===0){const v=Math.round(l*255);return [v,v,v];}
  const q=l<0.5?l*(1+s):l+s-l*s,p=2*l-q;
  const hue2=(t:number)=>{if(t<0)t+=1;if(t>1)t-=1;if(t<1/6)return p+(q-p)*6*t;if(t<1/2)return q;if(t<2/3)return p+(q-p)*(2/3-t)*6;return p;};
  return [Math.round(hue2(h+1/3)*255),Math.round(hue2(h)*255),Math.round(hue2(h-1/3)*255)];
}
function clamp(v:number){return Math.max(0,Math.min(255,v));}
function transformPalette(base:[number,number,number][],satMul:number,lightMul:number,hueShift:number):[number,number,number][] {
  return base.map(([r,g,b])=>{const [h,s,l]=rgbToHsl(r,g,b);return hslToRgb((h+hueShift+1)%1,Math.min(1,s*satMul),Math.min(0.95,l*lightMul));});
}

const PALETTES: Record<string,[number,number,number][]> = {
  authentic: DEFAULT_PALETTE,
  vibrant:   transformPalette(DEFAULT_PALETTE,1.6,1.05,0),
  warm:      transformPalette(DEFAULT_PALETTE,1.1,1.02,-0.03),
  cool:      transformPalette(DEFAULT_PALETTE,0.85,0.97,0.05),
  mono:      DEFAULT_PALETTE.map(([r,g,b])=>{const v=clamp(Math.round(0.2126*r+0.7152*g+0.0722*b));return [v,v,v];}),
  phosphor:  DEFAULT_PALETTE.map(([r,g,b])=>{const v=0.2126*r+0.7152*g+0.0722*b;return [clamp(Math.round(v*0.15)),clamp(Math.round(v*1.05)),clamp(Math.round(v*0.12))];}),
};

function applyPalette(name:string):void {
  const pal=PALETTES[name]; if (!pal) return;
  pal.forEach(([r,g,b],i)=>{currentPalette[i]=[r,g,b];setPaletteEntry(i,r,g,b);});
  buildPaletteGrid();
  document.querySelectorAll<HTMLButtonElement>(".preset-btn").forEach(b=>b.classList.toggle("active",b.dataset.preset===name));
}

document.querySelectorAll<HTMLButtonElement>(".preset-btn").forEach(btn=>btn.addEventListener("click",()=>applyPalette(btn.dataset.preset!)));
btnResetPal.addEventListener("click",()=>applyPalette("authentic"));

// ── Main loop ─────────────────────────────────────────────────────────────
function mainLoop(now: number): void {
  requestAnimationFrame(mainLoop);
  pollGamepad();
  if (!nes || paused) return;

  if (lastTime === 0) lastTime = now;
  accumulator += now - lastTime;
  lastTime = now;

  const effectiveFrameMs = FRAME_MS / speed;
  if (accumulator > 6 * effectiveFrameMs) accumulator = 6 * effectiveFrameMs;

  let ran = false;
  while (accumulator >= effectiveFrameMs) {
    nes.stepFrame();
    accumulator -= effectiveFrameMs;
    fpsFrames++;
    ran = true;
  }
  if (ran) {
    blit();
    if (speed === 1) pumpAudio();
    else { const s = new Float32Array(4096); nes.apu.drainSamples(s); } // discard at non-1x
  }

  if (now - fpsLast >= 1000) {
    fpsEl.textContent = `${fpsFrames} FPS`;
    fpsFrames = 0; fpsLast = now;
  }
}
requestAnimationFrame(mainLoop);

const romParam = new URLSearchParams(location.search).get("rom");
if (romParam) {
  fetch(romParam)
    .then(async r => { if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.arrayBuffer(); })
    .then(buf  => loadRom(new Uint8Array(buf), romParam.split("/").pop() ?? romParam))
    .catch(err => setStatus(`Fetch failed: ${err.message}`));
}
