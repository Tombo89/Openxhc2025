/*
 * xhc_display.h
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_XHC_DISPLAY_H_
#define INC_XHC_DISPLAY_H_

#include <stdint.h>

/* Init und 4-Zeilen-Anzeige (Zeile 0 = Header, 1..3 = Inhalt) */
void XHC_Display_Init(void);
void XHC_Display_SetHeader(const char *text);
void XHC_Display_SetLine(uint8_t line_idx, const char *text);  // 1..3

#endif /* INC_XHC_DISPLAY_H_ */
