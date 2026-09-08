# STM32 coursework target

`panel_hw.c` is the original libopencm3 GPIO/ADC driver, with the shared header now included. The game and emulator use zero-based row addresses, matching the driver's four raw address bits. The browser validates the software's RGB-plane convention; it does not establish the physical panel's chain direction or colour order. The lab handout describes a different colour-chain order, so these must be reconciled on the actual kit before flashing.

The supplied Makefile depends on a built `OPENCM3_DIR` and the **missing coursework `rules.mk`**, as well as the ARM cross compiler and board support. It now reports those prerequisites explicitly and includes `../src` in its source/header path. There is no self-contained firmware build in this checkout. Neither an STM32 build nor a hardware run was verified in the 2026 revision.

The driver's delay loop is not a calibrated millisecond timer. ADC setup, pin mode/calibration requirements and latch polarity also require verification against the kit documentation. A timer-driven scanout and sampled input would be the appropriate next step before making timing claims. The browser demonstration is independently buildable through the root Makefile and CI.
