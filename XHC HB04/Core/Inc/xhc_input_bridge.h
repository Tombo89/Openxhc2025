/*
 * xhc_input_bridge.h
 *
 *  Created on: Sep 2, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_XHC_INPUT_BRIDGE_H_
#define INC_XHC_INPUT_BRIDGE_H_

#ifndef XHC_INPUT_BRIDGE_H
#define XHC_INPUT_BRIDGE_H

#include <stdint.h>

void XHC_InputBridge_Init(void);
/* Bei jedem fertigen 0x06-Frame aufrufen: */
void XHC_InputBridge_SetDay(uint8_t day_from_host);
/* zyklisch in main-Schleife aufrufen: */
void XHC_InputBridge_Tick(void);

#endif



#endif /* INC_XHC_INPUT_BRIDGE_H_ */
