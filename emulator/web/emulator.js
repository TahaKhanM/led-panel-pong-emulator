/*
  emulator.js

  What this file does
  -------------------
  This file is the JavaScript "glue" that turns the Emscripten/WASM build of your Pong coursework
  into an interactive browser application.

  It has three jobs:

  1) Rendering:
     The C emulator (panel_emu.c) maintains a 32x32 RGB framebuffer in WASM memory. Each pixel is
     stored as 3 bytes [R,G,B] where each channel is either 0 or 1.

     panel_emu.c calls window.Emu.renderFrame(framebufferPtr, activeRowPair, displayOn) whenever the
     emulated panel latches new row data. This file reads the framebuffer bytes from the WASM heap
     and draws them onto a 32x32 <canvas> (scaled up by CSS).

  2) Input:
     The HTML page provides two "joystick" sliders (and keyboard controls that drive them). This
     file converts those slider positions into raw ADC-like values via window.Emu.getAdc(channel),
     matching what your game code expects when it calls getRawInput(channel).

  3) Runtime control:
     The browser UI includes Pause and Step buttons. panel_emu.c implements delay_ms() so that it
     can honour these controls without blocking the browser. This file exposes that state via
     window.Emu.isPaused() and window.Emu.consumeStep().

  Important design choice:
  - The C side already emulates the shift-register + latch + row-select behaviour. This file does
    not "redraw game objects" itself; it only displays the framebuffer produced by the C code.
*/

(function () {
  "use strict";

  // ---------------------------------------------------------------------------
  // Constants
  // ---------------------------------------------------------------------------

  // The LED panel is logically 32x32. The canvas is set to this size and then scaled by CSS.
  const PANEL_WIDTH_PIXELS = 32;
  const PANEL_HEIGHT_PIXELS = 32;

  // Slider range (matches the HTML <input type="range"> values).
  const SLIDER_ADC_MIN = 0;
  const SLIDER_ADC_MAX = 4095;

  // Joystick raw extremes expected by your game.c (measured values used during calibration).
  // Note: JOYSTICK_RAW_TOP should correspond to "paddle at the top".
  const JOYSTICK_RAW_TOP = 555;
  const JOYSTICK_RAW_BOTTOM = 105;

  // ---------------------------------------------------------------------------
  // High-level rendering mode
  // ---------------------------------------------------------------------------

  /*
    scanDisplayMode

    Controls how the canvas visualises the panel state.

    - "integrated": show the full framebuffer (what a person perceives after scan integration).
    - "row":        show only the currently selected row-pair (useful for debugging scanning).

    This is toggled with the 'L' key.
  */
  let scanDisplayMode = "integrated";

  // ---------------------------------------------------------------------------
  // Pause/step controls (used indirectly by delay_ms() in panel_emu.c)
  // ---------------------------------------------------------------------------

  /*
    isRuntimePaused

    When true, panel_emu.c's delay_ms() will yield until either:
      - the emulator is unpaused, or
      - a single-step token is consumed.
  */
  let isRuntimePaused = false;

  /*
    pendingSingleSteps

    The number of single-step "tokens" available. Each call to consumeStep() returns true once
    per token and decrements this counter.
  */
  let pendingSingleSteps = 0;

  // ---------------------------------------------------------------------------
  // Emscripten runtime handle and heap access
  // ---------------------------------------------------------------------------

  /*
    emscriptenModule

    Reference to the Emscripten Module object passed to onWasmReady(). Depending on how pong.js
    was generated, heap views may live on Module (Module.HEAPU8), on globalThis (HEAPU8), or be
    constructible from an exposed WebAssembly.Memory object.
  */
  let emscriptenModule = null;

  /*
    getWasmHeapU8

    Return a Uint8Array view over the WASM linear memory.

    Emscripten exposes the heap in slightly different ways depending on build settings. To keep
    this emulator robust, we support the common patterns:
      1) Module.HEAPU8
      2) globalThis.HEAPU8
      3) new Uint8Array(memory.buffer) if a WebAssembly.Memory is exposed

    If no heap is available yet, returns null.
  */
  function getWasmHeapU8() {
    if (emscriptenModule && emscriptenModule.HEAPU8 instanceof Uint8Array) {
      return emscriptenModule.HEAPU8;
    }

    if (globalThis.HEAPU8 instanceof Uint8Array) {
      return globalThis.HEAPU8;
    }

    const memory =
      (emscriptenModule && (emscriptenModule.wasmMemory || emscriptenModule["wasmMemory"])) ||
      (emscriptenModule && emscriptenModule.asm && (emscriptenModule.asm.memory || emscriptenModule.asm["memory"])) ||
      globalThis.wasmMemory;

    if (memory && memory.buffer) {
      return new Uint8Array(memory.buffer);
    }

    return null;
  }

  // ---------------------------------------------------------------------------
  // Small numeric helpers
  // ---------------------------------------------------------------------------

  /*
    clampToRange

    Constrain a numeric value to a closed interval [minValue, maxValue]. This is used primarily to
    keep slider-derived values within their expected bounds.
  */
  function clampToRange(value, minValue, maxValue) {
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
  }

  /*
    mapSliderToJoystickRaw

    Convert a slider position (0..4095) into the raw joystick ADC value expected by the original
    coursework calibration.

    Mapping used:
      slider == 0     => JOYSTICK_RAW_TOP
      slider == 4095  => JOYSTICK_RAW_BOTTOM

    This preserves the same direction convention used in the game code (top/bottom extremes).
  */
  function mapSliderToJoystickRaw(sliderValue) {
    const clamped = clampToRange(sliderValue, SLIDER_ADC_MIN, SLIDER_ADC_MAX);
    const t = clamped / SLIDER_ADC_MAX;
    return (JOYSTICK_RAW_TOP + t * (JOYSTICK_RAW_BOTTOM - JOYSTICK_RAW_TOP)) | 0;
  }

  // ---------------------------------------------------------------------------
  // Canvas rendering state
  // ---------------------------------------------------------------------------

  let panelCanvas = null;
  let panelContext = null;
  let panelImageData = null;
  let panelRgbaBytes = null;

  /*
    initialisePanelCanvas

    Locate the <canvas id="panel"> element, configure it to a logical resolution of 32x32 pixels,
    and pre-allocate an ImageData buffer that we reuse for each draw.

    The canvas is scaled up by CSS, while the underlying bitmap remains 32x32 so that each pixel
    corresponds exactly to one LED.
  */
  function initialisePanelCanvas() {
    panelCanvas = document.getElementById("panel");
    if (!panelCanvas) {
      throw new Error("Canvas #panel not found");
    }

    panelCanvas.width = PANEL_WIDTH_PIXELS;
    panelCanvas.height = PANEL_HEIGHT_PIXELS;

    panelContext = panelCanvas.getContext("2d", { alpha: false, desynchronized: true });
    panelImageData = panelContext.createImageData(PANEL_WIDTH_PIXELS, PANEL_HEIGHT_PIXELS);
    panelRgbaBytes = panelImageData.data;
  }

  /*
    drawPanelFromFramebufferPointer

    Render the WASM framebuffer onto the 32x32 canvas.

    Parameters:
      - framebufferPtr: pointer (in WASM memory) to PANEL_WIDTH_PIXELS*PANEL_HEIGHT_PIXELS*3 bytes.
                       Layout per pixel: [R,G,B] where each channel is 0 or 1.
      - activeRowPair:  0..15 representing the currently selected row address.
      - displayOn:      boolean controlling whether the display should appear enabled.

    Behaviour:
      - If displayOn is false, we draw a fully black panel.
      - If scanDisplayMode is "row", we only draw the active row-pair (top row r and bottom row r+16)
        and blank all other rows. This is a debug visualisation.
      - Otherwise we draw the full framebuffer.
  */
  function drawPanelFromFramebufferPointer(framebufferPtr, activeRowPair, displayOn) {
    if (!emscriptenModule) return;
    if (!panelRgbaBytes) initialisePanelCanvas();

    const heapU8 = getWasmHeapU8();
    if (!heapU8) return;

    const framebufferAddress = (framebufferPtr >>> 0);

    if (!displayOn) {
      for (let i = 0; i < panelRgbaBytes.length; i += 4) {
        panelRgbaBytes[i + 0] = 0;
        panelRgbaBytes[i + 1] = 0;
        panelRgbaBytes[i + 2] = 0;
        panelRgbaBytes[i + 3] = 255;
      }
      panelContext.putImageData(panelImageData, 0, 0);
      return;
    }

    const framebufferByteLength = PANEL_WIDTH_PIXELS * PANEL_HEIGHT_PIXELS * 3;
    if (framebufferAddress + framebufferByteLength > heapU8.length) {
      // Out of range: skip rather than crash.
      return;
    }

    const framebufferRgbBits = heapU8.subarray(framebufferAddress, framebufferAddress + framebufferByteLength);

    const selectedRowPair = (activeRowPair & 0x0f);
    const topRowIndex = selectedRowPair;
    const bottomRowIndex = selectedRowPair + 16;
    const showOnlyActiveRowPair = (scanDisplayMode === "row");

    for (let y = 0; y < PANEL_HEIGHT_PIXELS; y++) {
      const isRowVisible = !showOnlyActiveRowPair || (y === topRowIndex || y === bottomRowIndex);

      for (let x = 0; x < PANEL_WIDTH_PIXELS; x++) {
        const pixelIndex = (y * PANEL_WIDTH_PIXELS + x);
        const sourceIndex = pixelIndex * 3;
        const destIndex = pixelIndex * 4;

        const r = isRowVisible ? framebufferRgbBits[sourceIndex + 0] : 0;
        const g = isRowVisible ? framebufferRgbBits[sourceIndex + 1] : 0;
        const b = isRowVisible ? framebufferRgbBits[sourceIndex + 2] : 0;

        panelRgbaBytes[destIndex + 0] = r ? 255 : 0;
        panelRgbaBytes[destIndex + 1] = g ? 255 : 0;
        panelRgbaBytes[destIndex + 2] = b ? 255 : 0;
        panelRgbaBytes[destIndex + 3] = 255;
      }
    }

    panelContext.putImageData(panelImageData, 0, 0);
  }

  // ---------------------------------------------------------------------------
  // FPS counter (based on how often the panel latches)
  // ---------------------------------------------------------------------------

  let renderedLatchCount = 0;
  let lastFpsTimestampMs = performance.now();

  /*
    updateFpsReadoutLoop

    Update the on-screen FPS indicator once per second.

    We treat "FPS" as "how many latch-driven renders occurred per second". This is useful for
    validating scan performance and for ensuring the emulator remains responsive.
  */
  function updateFpsReadoutLoop() {
    const nowMs = performance.now();

    if (nowMs - lastFpsTimestampMs >= 1000) {
      const fps = (renderedLatchCount * 1000) / (nowMs - lastFpsTimestampMs);
      renderedLatchCount = 0;
      lastFpsTimestampMs = nowMs;

      if (window.EmuUI && typeof window.EmuUI.setFps === "function") {
        window.EmuUI.setFps(fps.toFixed(1));
      }
    }

    requestAnimationFrame(updateFpsReadoutLoop);
  }

  // ---------------------------------------------------------------------------
  // Diagnostics (one-time probe to confirm WASM heap access)
  // ---------------------------------------------------------------------------

  let hasLoggedInitialHeapProbe = false;

  /*
    logInitialHeapProbe

    Log a single diagnostic message the first time we receive a render callback from C.

    This helps verify that:
      - the framebuffer pointer is plausible,
      - the emulator can access the WASM heap,
      - and the first few bytes at that pointer can be read.

    It does not affect runtime behaviour beyond a single console log.
  */
  function logInitialHeapProbe(framebufferPtr) {
    if (hasLoggedInitialHeapProbe) return;
    hasLoggedInitialHeapProbe = true;

    const heapU8 = getWasmHeapU8();
    console.log("fbPtr:", framebufferPtr, "heap?", !!heapU8, "heapLen:", heapU8 ? heapU8.length : null);

    if (heapU8) {
      const p = (framebufferPtr >>> 0);
      console.log("first bytes:", heapU8.slice(p, p + 16));
    }
  }

  // ---------------------------------------------------------------------------
  // Public API called from panel_emu.c (via EM_JS)
  // ---------------------------------------------------------------------------

  const Emu = {
    /*
      onWasmReady

      Called by index.html once the Emscripten runtime signals that the WASM module has initialised.

      Responsibilities:
        - store the module reference so we can access the heap,
        - initialise the canvas,
        - wire up UI buttons (Start/Pause/Step/Reset),
        - install keyboard shortcuts (L toggles scan mode, Space toggles pause),
        - start the FPS update loop.
    */
    onWasmReady(moduleHandle) {
      emscriptenModule = moduleHandle;

      if (!panelCanvas) initialisePanelCanvas();

      if (window.EmuUI && typeof window.EmuUI.log === "function") {
        window.EmuUI.log("[emu] WASM runtime ready");
        window.EmuUI.log("[emu] Toggle scan mode with 'L' (integrated <-> row)");
      }

      const startButton = document.getElementById("btnStart");
      const pauseButton = document.getElementById("btnPause");
      const stepButton = document.getElementById("btnStep");
      const resetButton = document.getElementById("btnReset");

      if (startButton) startButton.addEventListener("click", () => { isRuntimePaused = false; });
      if (pauseButton) pauseButton.addEventListener("click", () => { isRuntimePaused = true; });
      if (stepButton) stepButton.addEventListener("click", () => {
        isRuntimePaused = true;
        pendingSingleSteps++;
      });
      if (resetButton) resetButton.addEventListener("click", () => location.reload());

      window.addEventListener("keydown", (e) => {
        if (e.code === "KeyL") {
          scanDisplayMode = (scanDisplayMode === "integrated") ? "row" : "integrated";

          if (window.EmuUI && typeof window.EmuUI.log === "function") {
            window.EmuUI.log("[emu] scanMode = " + scanDisplayMode);
          }
        }

        if (e.code === "Space") {
          isRuntimePaused = !isRuntimePaused;
        }
      });

      requestAnimationFrame(updateFpsReadoutLoop);
    },

    /*
      renderFrame

      Called by panel_emu.c whenever the emulated panel latches data (LatchRegister()).

      The parameters come directly from C:
        - framebufferPtr is a pointer into WASM memory
        - activeRowPair is the currently selected multiplexed row index
        - displayOn indicates whether the display is enabled

      We increment the FPS counter, optionally log the one-time heap probe, and then draw.
    */
    renderFrame(framebufferPtr, activeRowPair, displayOn) {
      renderedLatchCount++;

      logInitialHeapProbe(framebufferPtr);

      drawPanelFromFramebufferPointer(framebufferPtr | 0, activeRowPair | 0, !!displayOn);
    },

    /*
      getAdc

      Called by panel_emu.c from getRawInput(channel) to retrieve an emulated ADC value.

      The HTML UI provides each joystick position as a slider value (0..4095). We map that value
      into the raw range used by the original coursework calibration and return it for the
      requested channel.

      Your game code reads four channels (two per joystick). Because the browser slider represents
      a single continuous axis, we return the same value for the two channels associated with each
      joystick.
    */
    getAdc(channel) {
      const leftSliderValue = (window.EmuUI && window.EmuUI.getLeftADC) ? window.EmuUI.getLeftADC() : 2048;
      const rightSliderValue = (window.EmuUI && window.EmuUI.getRightADC) ? window.EmuUI.getRightADC() : 2048;

      const leftJoystickRaw = mapSliderToJoystickRaw(leftSliderValue);
      const rightJoystickRaw = mapSliderToJoystickRaw(rightSliderValue);

      switch (channel | 0) {
        case 1: return leftJoystickRaw;
        case 2: return leftJoystickRaw;
        case 6: return rightJoystickRaw;
        case 7: return rightJoystickRaw;
        default: return 0;
      }
    },

    /*
      isPaused

      Called by panel_emu.c delay_ms() to determine whether the emulator should pause execution.

      Returning true causes delay_ms() to yield until either the emulator is unpaused or a step token
      is consumed.
    */
    isPaused() {
      return isRuntimePaused;
    },

    /*
      consumeStep

      Called by panel_emu.c delay_ms() while paused.

      If at least one step token exists, we consume exactly one and return true to allow the C code
      to proceed for a single delay period. Otherwise returns false.
    */
    consumeStep() {
      if (pendingSingleSteps > 0) {
        pendingSingleSteps--;
        return true;
      }
      return false;
    },
  };

  // Expose the API for panel_emu.c.
  window.Emu = Emu;
})();
