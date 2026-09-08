/* Canvas rendering consumes the current HEAPU8 view passed by the C bridge. */


(function () {
  "use strict";

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

  let scanDisplayMode = "integrated";

  let isRuntimePaused = false;

  let pendingSingleSteps = 0;

  let emscriptenModule = null;


  function clampToRange(value, minValue, maxValue) {
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
  }

  function mapSliderToJoystickRaw(sliderValue) {
    const clamped = clampToRange(sliderValue, SLIDER_ADC_MIN, SLIDER_ADC_MAX);
    const t = clamped / SLIDER_ADC_MAX;
    return (JOYSTICK_RAW_TOP + t * (JOYSTICK_RAW_BOTTOM - JOYSTICK_RAW_TOP)) | 0;
  }

  let panelCanvas = null;
  let panelContext = null;
  let panelImageData = null;
  let panelRgbaBytes = null;

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

  function drawPanelFromFramebufferPointer(framebufferPtr, activeRowPair, displayOn, heapU8) {
    if (!emscriptenModule) return;
    if (!panelRgbaBytes) initialisePanelCanvas();

    if (!(heapU8 instanceof Uint8Array)) return;

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

  let renderedLatchCount = 0;
  let lastFpsTimestampMs = performance.now();

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

  const Emu = {

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

      if (startButton) startButton.addEventListener("click", () => { isRuntimePaused = false; pendingSingleSteps = 0; });
      if (pauseButton) pauseButton.addEventListener("click", () => { isRuntimePaused = true; });
      if (stepButton) stepButton.addEventListener("click", () => {
        isRuntimePaused = true;
        pendingSingleSteps++;
      });
      if (resetButton) resetButton.addEventListener("click", () => location.reload());

      window.addEventListener("keydown", (e) => {
        if (e.code === "KeyL" && !e.repeat) {
          scanDisplayMode = (scanDisplayMode === "integrated") ? "row" : "integrated";

          if (window.EmuUI && typeof window.EmuUI.log === "function") {
            window.EmuUI.log("[emu] scanMode = " + scanDisplayMode);
          }
        }

        if (e.code === "Space" && e.target === document.body) {
          e.preventDefault();
          if (e.repeat) return;
          pendingSingleSteps = 0;
          isRuntimePaused = !isRuntimePaused;
        }
      });

      requestAnimationFrame(updateFpsReadoutLoop);
    },

    renderFrame(framebufferPtr, activeRowPair, displayOn, heapU8) {
      renderedLatchCount++;


      drawPanelFromFramebufferPointer(framebufferPtr | 0, activeRowPair | 0, !!displayOn, heapU8);
      window.EmuUI?.setDisplayEnabled(true);
    },

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

    isPaused() {
      return isRuntimePaused;
    },

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
