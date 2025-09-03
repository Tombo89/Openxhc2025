#include "io_kbd.h"
#include <string.h>

/* Row/Col Helfer */
static inline void row_hi(uint8_t r){
  switch(r){
    case 0: HAL_GPIO_WritePin(KBD_ROW1_GPIO_Port, KBD_ROW1_Pin, GPIO_PIN_SET); break;
    case 1: HAL_GPIO_WritePin(KBD_ROW2_GPIO_Port, KBD_ROW2_Pin, GPIO_PIN_SET); break;
    case 2: HAL_GPIO_WritePin(KBD_ROW3_GPIO_Port, KBD_ROW3_Pin, GPIO_PIN_SET); break;
    case 3: HAL_GPIO_WritePin(KBD_ROW4_GPIO_Port, KBD_ROW4_Pin, GPIO_PIN_SET); break;
  }
}
static inline void row_lo(uint8_t r){
  switch(r){
    case 0: HAL_GPIO_WritePin(KBD_ROW1_GPIO_Port, KBD_ROW1_Pin, GPIO_PIN_RESET); break;
    case 1: HAL_GPIO_WritePin(KBD_ROW2_GPIO_Port, KBD_ROW2_Pin, GPIO_PIN_RESET); break;
    case 2: HAL_GPIO_WritePin(KBD_ROW3_GPIO_Port, KBD_ROW3_Pin, GPIO_PIN_RESET); break;
    case 3: HAL_GPIO_WritePin(KBD_ROW4_GPIO_Port, KBD_ROW4_Pin, GPIO_PIN_RESET); break;
  }
}
static inline uint8_t col_active(uint8_t c){ // aktiv LOW (PU)
  switch(c){
    case 0: return HAL_GPIO_ReadPin(KBD_COL1_GPIO_Port, KBD_COL1_Pin) == GPIO_PIN_RESET;
    case 1: return HAL_GPIO_ReadPin(KBD_COL2_GPIO_Port, KBD_COL2_Pin) == GPIO_PIN_RESET;
    case 2: return HAL_GPIO_ReadPin(KBD_COL3_GPIO_Port, KBD_COL3_Pin) == GPIO_PIN_RESET;
    case 3: return HAL_GPIO_ReadPin(KBD_COL4_GPIO_Port, KBD_COL4_Pin) == GPIO_PIN_RESET;
  }
  return 0;
}

/* Matrix-Mapping (vertikal gespiegelt wie im Altcode) */
static const uint8_t keymap[4][4] = {
  /* row0 */ { BTN_SS,     BTN_Zero,   BTN_Macro1,  BTN_Stop    },
  /* row1 */ { BTN_Spindle,BTN_SafeZ,  BTN_ProbeZ,  BTN_Goto0   },
  /* row2 */ { BTN_Macro7, BTN_Macro6, BTN_Macro3,  BTN_GotoHome},
  /* row3 */ { BTN_Macro2, BTN_Rewind, BTN_Half,    BTN_Step    },
};

static uint8_t  deb_cnt[4][4];
static uint8_t  deb_state[4][4];  // 0=off,1=on (debounced)
static uint8_t  last_state[4][4];
static uint8_t  q[2];             // kleine Queue für bis zu 2 Codes
static uint8_t  q_len = 0;

static uint8_t  cur_row = 0;
static uint8_t  settled = 0;

/* Public */
void KBD_Init(void){
  // alle Rows in Hi-Z (via OD + SET)
  for(uint8_t r=0;r<4;r++) row_hi(r);
  memset(deb_cnt, 0, sizeof(deb_cnt));
  memset(deb_state, 0, sizeof(deb_state));
  memset(last_state,0, sizeof(last_state));
  q_len = 0;
  cur_row = 0;
  settled = 0;
}

/* 1x/ms: eine Row aktivieren und Columns dieser Row lesen */
void KBD_Tick1ms(void)
{
  // Vorige Row wieder High-Z
  row_hi(cur_row);

  // nächste Row wählen
  cur_row = (uint8_t)((cur_row + 1u) & 3u);

  // neue Row aktivieren
  row_lo(cur_row);
  settled = 1; // 1ms settle ist reichlich

  if (!settled) return;

  // Columns der aktiven Row einlesen & entprellen
  for(uint8_t c=0;c<4;c++){
    uint8_t raw_on = col_active(c); // 1 = gedrückt

    // Debounce: der Zähler zählt Richtung stabiler Zustand
    uint8_t target = raw_on ? 1u : 0u;
    if (deb_state[cur_row][c] != target){
      if (deb_cnt[cur_row][c] < KBD_DEBOUNCE_MS) deb_cnt[cur_row][c]++;
      if (deb_cnt[cur_row][c] >= KBD_DEBOUNCE_MS){
        deb_state[cur_row][c] = target;
        deb_cnt[cur_row][c] = 0;

        // Edge (nur Press erzeugt Code; Release ignorieren)
        if (target == 1u){
          if (q_len < 2){
            q[q_len++] = keymap[cur_row][c];
          }
        }
      }
    } else {
      deb_cnt[cur_row][c] = 0;
    }
    last_state[cur_row][c] = raw_on;
  }
}

/* Liefert bis zu 2 Codes seit letztem Fetch, dann leert die Queue */
void KBD_Fetch(uint8_t *c1, uint8_t *c2)
{
  if (c1) *c1 = 0;
  if (c2) *c2 = 0;
  if (q_len == 0) return;

  if (c1) *c1 = q[0];
  if (q_len >= 2 && c2) *c2 = q[1];

  q_len = 0;
}
