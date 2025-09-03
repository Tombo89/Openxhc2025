/*
 * io_kbd.h
 *
 *  Created on: Sep 3, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_IO_KBD_H_
#define INC_IO_KBD_H_

#pragma once
#pragma once
#include <stdint.h>
#include "stm32f1xx_hal.h"

/* --- Pins (CubeMX so konfigurieren):
 * ROWx: Open-Drain Output, initial HIGH (Hi-Z)
 * COLx: Input Pull-Up
 * Default: PB12..PB15 (ROW), PB5..PB8 (COL) – anpassbar via -D defines
*/
#ifndef KBD_ROW1_GPIO_Port
 #define KBD_ROW1_GPIO_Port GPIOB
 #define KBD_ROW1_Pin       GPIO_PIN_12
 #define KBD_ROW2_GPIO_Port GPIOB
 #define KBD_ROW2_Pin       GPIO_PIN_13
 #define KBD_ROW3_GPIO_Port GPIOB
 #define KBD_ROW3_Pin       GPIO_PIN_14
 #define KBD_ROW4_GPIO_Port GPIOB
 #define KBD_ROW4_Pin       GPIO_PIN_15
#endif

#ifndef KBD_COL1_GPIO_Port
 #define KBD_COL1_GPIO_Port GPIOB
 #define KBD_COL1_Pin       GPIO_PIN_5
 #define KBD_COL2_GPIO_Port GPIOB
 #define KBD_COL2_Pin       GPIO_PIN_6
 #define KBD_COL3_GPIO_Port GPIOB
 #define KBD_COL3_Pin       GPIO_PIN_7
 #define KBD_COL4_GPIO_Port GPIOB
 #define KBD_COL4_Pin       GPIO_PIN_8
#endif

/* Entprellzeit (ms) */
#ifndef KBD_DEBOUNCE_MS
 #define KBD_DEBOUNCE_MS 8u
#endif

/* Keycodes (wie dein Altprojekt) */
#define BTN_Reset   0x17
#define BTN_Stop    0x16
#define BTN_Goto0   0x01
#define BTN_SS      0x02
#define BTN_Rewind  0x03
#define BTN_ProbeZ  0x04
#define BTN_Spindle 0x0C
#define BTN_Half    0x06
#define BTN_Zero    0x07
#define BTN_SafeZ   0x08
#define BTN_GotoHome 0x09
#define BTN_Macro1  0x0A
#define BTN_Macro2  0x0B
#define BTN_Macro3  0x05
#define BTN_Macro6  0x0F
#define BTN_Macro7  0x10
#define BTN_Step    0x0D
#define BTN_MPG     0x0E

void KBD_Init(void);          // setzt Rows high (Hi-Z)
void KBD_Tick1ms(void);       // 1x pro ms aufrufen (scan + debounce)
void KBD_Fetch(uint8_t *c1, uint8_t *c2); // liefert bis zu 2 Keycodes (edge-basiert)

#endif /* INC_IO_KBD_H_ */
