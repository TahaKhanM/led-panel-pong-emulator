# LED Panel Pong + Browser Emulator (CS132 Coursework)

This repository contains a Pong-style game written in C for a **32×32 RGB LED matrix** (driven as a multiplexed, shift-register-based panel) an most importantl a **browser emulator** that lets you run and debug the same embedded game logic without hardware.

The emulator is the “unique” part of the coursework: it faithfully emulates the panel’s **bitstream protocol** (shift + latch + row address) and maps joystick input to ADC-like readings, so the original game code can run unchanged.

## Quick start (browser emulator)

### 1) Build (Emscripten)

You need Emscripten (`emcc`) on your PATH.

```bash
./emulator/scripts/build_web.sh
```

This compiles:
- `src/game.c` (game logic + scanout)
- `emulator/src/panel_emu.c` (Web/WASM HAL)

…and produces:
- `emulator/web/pong.js`
- `emulator/web/pong.wasm`

(Compile flags match the project write-up: `-O2 -sASYNCIFY -sALLOW_MEMORY_GROWTH`.)

### 2) Run

```bash
./emulator/scripts/serve.sh
```

Then open `http://localhost:8000` in a browser.

Controls:
- Left paddle: **W / S** (or on-screen ▲/▼)
- Right paddle: **↑ / ↓** (or on-screen ▲/▼)
- Toggle scan visualisation: **L** (integrated view ↔ active row-pair debug view)
- Pause/Step: buttons on the page (space also toggles pause)

## What makes the emulator interesting

Most “embedded emulators” cheat by exposing a framebuffer API (“draw pixel x,y”). This emulator is deliberately lower-level: it reproduces the exact update protocol the physical panel expects.

### The HAL boundary (`panel.h`)

`src/panel.h` defines the hardware abstraction layer (HAL) used by the game:

- Panel output primitives: `PrepareLatch`, `PushBit`, `SelectRow`, `LatchRegister`, `ClearRow`
- Input/timing: `getRawInput`, `delay_ms`, plus `setupPanel` / `setupInput`

The game code (`src/game.c`) only calls these functions. At build time, you pick one implementation:

- **Hardware target:** `hardware/panel_hw.c` (STM32 GPIO + ADC via libopencm3)
- **Web target:** `emulator/src/panel_emu.c` (WASM + JavaScript rendering/input)

### Faithful panel emulation (`panel_emu.c`)

The physical 32×32 panel is refreshed as **two 16-row halves** (row-pairs). For each row-pair, the game shifts **192 bits**:

- Top half (row `r`): 32×(R,G,B) = 96 bits
- Bottom half (row `r+16`): 32×(R,G,B) = 96 bits
- Total: 192 bits per row-pair

`panel_emu.c` emulates that behaviour explicitly:

1. **Shift register model**: stores the most recent 192 pushed bits (like a fixed-length register chain).
2. **Latch commit** (`LatchRegister`): decodes those 192 bits into a 32×32 RGB framebuffer (1-bit per channel).
3. **Render callback**: calls `window.Emu.renderFrame(...)` so JavaScript can draw the framebuffer to a canvas.

Timing is also made “browser-safe”: `delay_ms()` uses `emscripten_sleep()` (enabled by `-sASYNCIFY`) so the UI stays responsive, and it honours Pause/Step controls.

### JavaScript glue (`emulator.js` + `index.html`)

- `emulator/web/index.html` hosts the UI (canvas, sliders, buttons) and wires Emscripten stdout/stderr into an on-page console.
- `emulator/web/emulator.js` implements:
  - `window.Emu.renderFrame(ptr, row, on)` → reads WASM memory and draws the 32×32 canvas
  - `window.Emu.getAdc(channel)` → converts slider/keyboard state into ADC-like values
  - pause/step state queried by `panel_emu.c`

A useful debugging feature is the **row-scan view** (`L` key): instead of drawing the whole integrated framebuffer, the page can show only the currently selected row-pair. This makes scan-order and latch timing issues much easier to see.

## Repository layout

```
.
├─ src/                     # shared code (runs on both targets)
│  ├─ game.c
│  └─ panel.h
├─ emulator/                # browser emulator target (the focus)
│  ├─ src/
│  │  └─ panel_emu.c
│  ├─ web/
│  │  ├─ index.html
│  │  ├─ emulator.js
│  │  └─ pong.js            # Emscripten output (pong.wasm generated alongside)
│  └─ scripts/
│     ├─ build_web.sh
│     └─ serve.sh
├─ hardware/                # STM32 target (coursework hardware build)
│  ├─ panel_hw.c
│  └─ Makefile
└─ docs/
   ├─ CS132_Report_Draft_2.pdf
   └─ CS132_LED_Panel.pdf
```

## Hardware build notes (STM32)

The coursework hardware build uses `hardware/Makefile` and `libopencm3`.

Typical workflow (as described in the write-up):

```bash
make
st-flash --reset write ledpanel.bin 0x8000000
```

Note: the provided `Makefile` references additional build system files (e.g. `rules.mk`, `OPENCM3_DIR`) that are usually supplied by the coursework environment.

## Summary of the development process

- **Reverse-engineered** the LED panel’s refresh protocol (row multiplexing + shift register + latch timing) and joystick ADC usage.
- **Separated concerns** by designing a clean HAL (`panel.h`) so the *same* game logic could run on both hardware and the web.
- **Built a faithful emulator** rather than a shortcut framebuffer API:
  - Emulated the shift register contents and only updated the framebuffer on latch.
  - Added scan-debug visualisation to spot ordering/timing mistakes.
- **Ported timing/input to the browser**:
  - Used `emscripten_sleep` + Asyncify to avoid blocking the UI thread.
  - Mapped joystick sliders/keys to realistic ADC ranges to preserve calibration assumptions.

## What I learned (technical)

- How multiplexed LED matrices work in practice: **row addressing**, **shift-register chains**, and why the **bit order** and **latch timing** are everything.
- How to design an interface that survives multiple targets: a small, stable HAL made it possible to keep `game.c` unchanged.
- How to make embedded-style delays work in a browser: **Asyncify** + `emscripten_sleep` enables cooperative timing without freezing the page.
- How to debug hardware protocols faster by building tooling: the scan-row visualisation is essentially an “oscilloscope view” for panel refresh.

---

### Missing files / artefacts

If you expected other coursework-provided files (e.g. `rules.mk` for STM32 builds) or a pre-built `pong.wasm` to be committed for instant demo, upload them and they can be dropped into this structure without changing any source.
