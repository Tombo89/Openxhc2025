/*
 * io_kbd.h
 *
 *  Created on: Sep 3, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_IO_KBD_H_
#define INC_IO_KBD_H_

#pragma once
#include <stdint.h>

/* --- Button-Codes (wie altes Projekt) --- */
#define BTN_Reset    0x17
#define BTN_Stop     0x16
#define BTN_Goto0    0x01
#define BTN_SS       0x02
#define BTN_Rewind   0x03
#define BTN_ProbeZ   0x04
#define BTN_Spindle  0x0C
#define BTN_Half     0x06
#define BTN_Zero     0x07
#define BTN_SafeZ    0x08
#define BTN_GotoHome 0x09
#define BTN_Macro1   0x0A
#define BTN_Macro2   0x0B
#define BTN_Macro3   0x05
#define BTN_Macro6   0x0F
#define BTN_Macro7   0x10
#define BTN_Step     0x0D
#define BTN_MPG      0x0E   /* (falls gebraucht) */

/* Optional: Scan-Tuning */
#ifndef KBD_SETTLE_NOPS
#define KBD_SETTLE_NOPS  40u  /* kleine Entladezeit nach Row=LOW */
#endif

/* API */
void   KBD_Init(void);
void   KBD_Deinit(void);
/* liest bis zu 2 Keys; gibt Anzahl (0..2) zurück.
   c1/c2 werden auf 0 gesetzt, wenn nicht belegt */
uint8_t KBD_Read(uint8_t *c1, uint8_t *c2);


#endif /* INC_IO_KBD_H_ */
