/*
 * io_inputs.h
 *
 *  Created on: Sep 2, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_IO_INPUTS_H_
#define INC_IO_INPUTS_H_

#pragma once
#include <stdint.h>
#include "stm32f1xx_hal.h"

/* Rückgabecodes für den Rotary-Schalter (wie Alt-Code) */
#define XHC_ROT_OFF   0x00
#define XHC_ROT_X     0x11
#define XHC_ROT_Y     0x12
#define XHC_ROT_Z     0x13
#define XHC_ROT_S     0x15  /* Spindle speed */
#define XHC_ROT_F     0x14  /* Feedrate override */
#define XHC_ROT_PROC  0x18  /* Processing speed / A-Achse HB04 */

#ifdef __cplusplus
extern "C" {
#endif

/* Einmalig nach MX_GPIO_Init() & Timer-Init aufrufen */
void IOInputs_Init(void);

/* Optional: Aufräumen (nicht zwingend nötig) */
void IOInputs_Deinit(void);

/* Encoder: Anzahl Rastungen seit letztem Aufruf (-/+) */
int16_t IOInputs_EncoderReadDetents(void);

/* Rotary: stabil entprellter Code (s.o.). Gibt OFF, wenn keine Position aktiv. */
uint8_t IOInputs_RotaryReadCode(void);

/* Optional: HW-Typ lesen (HIGH = HB04 je nach Verdrahtung) */
uint8_t IOInputs_HwIsHB04(void);

/* Optional: Positionsmodus (1 = WC aktiv), falls SELECT_POS verbaut ist */
uint8_t IOInputs_PosIsWC(void);

#ifdef __cplusplus
}
#endif


#endif /* INC_IO_INPUTS_H_ */
