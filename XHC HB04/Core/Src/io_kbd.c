/*
 * io_kbd.c
 *
 *  Created on: Sep 3, 2025
 *      Author: Thomas Weckmann
 */


#include "io_kbd.h"
#include "stm32f1xx_hal.h"

/* ===== PIN-ZUORDNUNG (an deine CubeMX-Labels anpassen, sonst Fallbacks) ===== */
/* COLs: Input Pull-Up */
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
/* ROWs: Open-Drain Output (Hi-Z = HIGH, aktiv = LOW) */
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

/* Schnelle Helpers */
static inline void row_drive_low(GPIO_TypeDef *port, uint16_t pin)  { HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET); }
static inline void row_hiz_high(GPIO_TypeDef *port, uint16_t pin)   { HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);   }
static inline uint8_t col_is_low(GPIO_TypeDef *port, uint16_t pin)  { return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET); }

/* Reihenfolge: row 0..3, col 0..3 */
static const uint8_t kbd_key_codes[4][4] = {
  /* row0: reset, stop, m1, m2  (spiegelung wie im alten Projekt) */
  { BTN_SS,     BTN_Zero,   BTN_Macro1,   BTN_Stop     },
  /* row1: GoZero, s/p, rewind, ProbeZ */
  { BTN_Spindle,BTN_SafeZ,  BTN_ProbeZ,   BTN_Goto0    },
  /* row2: Spind, =1/2, =0, Safe-Z */
  { BTN_Macro7, BTN_Macro6, BTN_Macro3,   BTN_GotoHome },
  /* row3: Home, M6, Step+, M3 */
  { BTN_Macro2, BTN_Rewind, BTN_Half,     BTN_Step     },
};

static void rows_all_hiz(void)
{
  row_hiz_high(KBD_ROW1_GPIO_Port, KBD_ROW1_Pin);
  row_hiz_high(KBD_ROW2_GPIO_Port, KBD_ROW2_Pin);
  row_hiz_high(KBD_ROW3_GPIO_Port, KBD_ROW3_Pin);
  row_hiz_high(KBD_ROW4_GPIO_Port, KBD_ROW4_Pin);
}

static void row_set_active(uint8_t r)
{
  rows_all_hiz();
  switch (r) {
    case 0: row_drive_low(KBD_ROW1_GPIO_Port, KBD_ROW1_Pin); break;
    case 1: row_drive_low(KBD_ROW2_GPIO_Port, KBD_ROW2_Pin); break;
    case 2: row_drive_low(KBD_ROW3_GPIO_Port, KBD_ROW3_Pin); break;
    case 3: row_drive_low(KBD_ROW4_GPIO_Port, KBD_ROW4_Pin); break;
  }
  /* kleine Settling-Zeit (ohne HAL_Delay) */
  for (volatile uint32_t i=0; i<KBD_SETTLE_NOPS; ++i) __NOP();
}

void KBD_Init(void)
{
  /* Optional: GPIO-Clocks sicherstellen (falls nicht über MX_GPIO_Init() getan) */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef gi;

  /* COLs: Input Pull-Up */
  gi.Mode  = GPIO_MODE_INPUT;
  gi.Pull  = GPIO_PULLUP;
  gi.Speed = GPIO_SPEED_FREQ_LOW;

  gi.Pin = KBD_COL1_Pin; HAL_GPIO_Init(KBD_COL1_GPIO_Port, &gi);
  gi.Pin = KBD_COL2_Pin; HAL_GPIO_Init(KBD_COL2_GPIO_Port, &gi);
  gi.Pin = KBD_COL3_Pin; HAL_GPIO_Init(KBD_COL3_GPIO_Port, &gi);
  gi.Pin = KBD_COL4_Pin; HAL_GPIO_Init(KBD_COL4_GPIO_Port, &gi);

  /* ROWs: Open-Drain Output, initial HIGH (Hi-Z über externen PU der COLs) */
  gi.Mode  = GPIO_MODE_OUTPUT_OD;
  gi.Pull  = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;

  gi.Pin = KBD_ROW1_Pin; HAL_GPIO_Init(KBD_ROW1_GPIO_Port, &gi);
  gi.Pin = KBD_ROW2_Pin; HAL_GPIO_Init(KBD_ROW2_GPIO_Port, &gi);
  gi.Pin = KBD_ROW3_Pin; HAL_GPIO_Init(KBD_ROW3_GPIO_Port, &gi);
  gi.Pin = KBD_ROW4_Pin; HAL_GPIO_Init(KBD_ROW4_GPIO_Port, &gi);

  rows_all_hiz();
}

void KBD_Deinit(void)
{
  /* pins optional zurück auf input floating */
  GPIO_InitTypeDef gi = {0};
  gi.Mode = GPIO_MODE_INPUT;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;

  gi.Pin = KBD_COL1_Pin; HAL_GPIO_Init(KBD_COL1_GPIO_Port, &gi);
  gi.Pin = KBD_COL2_Pin; HAL_GPIO_Init(KBD_COL2_GPIO_Port, &gi);
  gi.Pin = KBD_COL3_Pin; HAL_GPIO_Init(KBD_COL3_GPIO_Port, &gi);
  gi.Pin = KBD_COL4_Pin; HAL_GPIO_Init(KBD_COL4_GPIO_Port, &gi);

  gi.Pin = KBD_ROW1_Pin; HAL_GPIO_Init(KBD_ROW1_GPIO_Port, &gi);
  gi.Pin = KBD_ROW2_Pin; HAL_GPIO_Init(KBD_ROW2_GPIO_Port, &gi);
  gi.Pin = KBD_ROW3_Pin; HAL_GPIO_Init(KBD_ROW3_GPIO_Port, &gi);
  gi.Pin = KBD_ROW4_Pin; HAL_GPIO_Init(KBD_ROW4_GPIO_Port, &gi);
}

/* return: Anzahl Tasten (0..2). c1/c2 werden stets gesetzt (0 wenn leer). */
uint8_t KBD_Read(uint8_t *c1, uint8_t *c2)
{
  if (c1) *c1 = 0;
  if (c2) *c2 = 0;

  uint8_t found = 0;

  for (uint8_t row=0; row<4; ++row) {
    row_set_active(row);

    /* COLs prüfen (aktiv LOW) */
    for (uint8_t col=0; col<4; ++col) {
      uint8_t pressed = 0;
      switch (col) {
        case 0: pressed = col_is_low(KBD_COL1_GPIO_Port, KBD_COL1_Pin); break;
        case 1: pressed = col_is_low(KBD_COL2_GPIO_Port, KBD_COL2_Pin); break;
        case 2: pressed = col_is_low(KBD_COL3_GPIO_Port, KBD_COL3_Pin); break;
        case 3: pressed = col_is_low(KBD_COL4_GPIO_Port, KBD_COL4_Pin); break;
      }
      if (!pressed) continue;

      uint8_t code = kbd_key_codes[row][col];
      if (code == 0x00) continue; /* unbenutzt */

      if (found == 0) {
        if (c1) *c1 = code;
        found = 1;
      } else if (found == 1) {
        if (c2) *c2 = code;
        rows_all_hiz();
        return 2; /* maximal zwei */
      }
    }
  }

  rows_all_hiz();
  return found; /* 0 oder 1 */
}
