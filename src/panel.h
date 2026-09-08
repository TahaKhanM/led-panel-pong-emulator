#ifndef PANEL_API_H
#define PANEL_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Link exactly one implementation: panel_hw.c or panel_emu.c. */
void setupPanel(void);
void setupInput(void);
uint32_t getRawInput(int channelValue);
/* Browser builds yield cooperatively; hardware timing needs calibration. */
void delay_ms(uint32_t ms);
/* Shifted data becomes visible only on LatchRegister. */
void PrepareLatch(void);
void LatchRegister(void);
/* Four-bit, zero-based row-pair address: r and r+16. */
void SelectRow(int row);
/* RGB planes, 32 bits each, top half then bottom half: 192 bits. */
void PushBit(int onoff);
/* Shift 192 zeros for this address; does not latch. */
void ClearRow(int row);
#ifdef __cplusplus
}
#endif
#endif
