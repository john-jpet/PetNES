# PetNES

A web NES emulator with a C++ core compiled to WebAssembly via Emscripten. Drop in a ROM and play.

## Features

- **Accurate emulation** — 6502 CPU (all 256 opcodes including unofficials), 2C02 PPU with loopy scroll, 2A03 APU with non-linear mixing
- **Mapper support** — NROM (0), MMC1 (1), UxROM (2), CNROM (3), MMC3 (4), MMC2 (9 — Punch-Out!!), GxROM (66)
- **WebGL CRT renderer** — scanlines, barrel distortion, phosphor bloom, vignette
- **ROM library** — IndexedDB-backed, remembers your games with thumbnails
- **Save states** — serialize/deserialize full emulator state via nlohmann/json
- **Channel muting** — toggle any of the 5 APU channels live
- **Palette editor** — edit all 64 NES palette entries with 6 built-in presets
- **Speed control** — ½×, 1×, 2×, 4×
- **Gamepad support** — standard gamepad API
- **Screenshot** — one-click PNG download
- **Fullscreen** — browser fullscreen with correct aspect ratio

## Stack

| Layer | Technology |
|---|---|
| Emulation core | C++20 |
| Compilation target | WebAssembly (Emscripten 6.0.1) |
| Build system | CMake 4.x |
| Frontend | TypeScript + Vite |
| Rendering | WebGL2 |
| Audio | AudioWorklet |
| Storage | IndexedDB |

## Prerequisites

- [Node.js](https://nodejs.org/) 18+
- [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (emsdk) — activate with `emsdk activate latest`
- CMake 3.20+

## Getting started

```bash
npm install

# Build WASM core (requires emcc on PATH)
npm run build:wasm

# Start dev server
npm run dev
```

Open `http://localhost:5173` and drag a `.nes` ROM onto the page.

## Building

```bash
# Full production build
npm run build

# WASM core only
npm run build:wasm

# Native binary (for nestest)
npm run build:native

# Run nestest accuracy suite
npm run test:nestest
```

## Project structure

```
src/
  core/           C++ emulation core (cartridge, mapper, cpu, ppu, apu, bus, nes)
    vendor/       nlohmann/json (header-only)
  bindings.cpp    Emscripten C API
  nes-wasm.ts     TypeScript wrapper around WASM module
  main.ts         Frontend entry point
  crt.ts          WebGL CRT renderer
  lib.ts          IndexedDB ROM library
public/
  apu-worklet.js  AudioWorklet processor
  wasm/           WASM binary served statically
wasm-out/         Emscripten JS glue output (built, not committed)
test/
  nestest_runner.cpp  CPU accuracy test harness
  roms/               Test ROMs (not included)
```

## Controls

| NES Button | Keyboard |
|---|---|
| D-Pad | Arrow keys |
| A | X |
| B | Z |
| Start | Enter |
| Select | Shift |

Gamepad is supported natively via the Web Gamepad API.

## Legal

PetNES does not include any ROM files. You are responsible for ensuring you have the legal right to use any ROM you load. The emulator is provided for preservation, homebrew development, and educational purposes.
