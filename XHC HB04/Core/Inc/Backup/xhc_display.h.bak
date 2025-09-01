/*
 * xhc_display.h
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_XHC_DISPLAY_H_
#define INC_XHC_DISPLAY_H_

#pragma once
#include <stdint.h>

/* Init und bis zu 6 Content-Zeilen:
   Zeile 0 = Header, Zeilen 1..6 = Inhalt */
void XHC_Display_Init(void);
void XHC_Display_SetHeader(const char *text);
void XHC_Display_SetLine(uint8_t line_idx, const char *text);  // 1..6

#endif /* INC_XHC_DISPLAY_H_ */
