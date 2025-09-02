/*
 * io_input.c
 *  Created on: Sep 2, 2025
 *      Author: Thomas Weckmann
 */

#include "io_inputs.h"
#include "main.h"
#include "stm32f1xx_hal_tim.h"

extern TIM_HandleTypeDef htim2;     // kommt aus tim.c/main.c

/* --- Encoder-Timer Handle als Macro kapseln (einheitlich nutzen) --- */
#ifndef XHC_ENC_TIM
  #define XHC_ENC_TIM htim2
#endif

/* --- Encoder-Detent-Auflösung (4 Flanken = 1 Rastung) --- */
#ifndef XHC_ENC_PULSES_PER_DETENT
  #define XHC_ENC_PULSES_PER_DETENT  4
#endif

/* --- Rotary-Pins (Fallback, falls keine CubeMX-Labels existieren) --- */
#ifndef XHC_ROT1_GPIO_Port
  #define XHC_ROT1_GPIO_Port GPIOA
  #define XHC_ROT1_Pin       GPIO_PIN_8   /* P1 -> X */
  #define XHC_ROT2_GPIO_Port GPIOA
  #define XHC_ROT2_Pin       GPIO_PIN_9   /* P2 -> Y */
  #define XHC_ROT3_GPIO_Port GPIOA
  #define XHC_ROT3_Pin       GPIO_PIN_10  /* P3 -> Z */
  #define XHC_ROT4_GPIO_Port GPIOB
  #define XHC_ROT4_Pin       GPIO_PIN_10  /* P4 -> S (Spindle) */
  #define XHC_ROT5_GPIO_Port GPIOB
  #define XHC_ROT5_Pin       GPIO_PIN_11  /* P5 -> F (Feed) */
  #define XHC_ROT6_GPIO_Port GPIOB
  #define XHC_ROT6_Pin       GPIO_PIN_1   /* P6 -> Processing/A */
#endif

/* --- Entprellzeit Rotary (ms) --- */
#ifndef XHC_ROT_DEBOUNCE_MS
  #define XHC_ROT_DEBOUNCE_MS  5u
#endif

/* ========= interne Stati (GENAU EINMAL!) ========= */
static uint16_t s_enc_prev_cnt = 0;
static int32_t  s_enc_accum    = 0;

static uint8_t  s_rot_last_raw   = 0;
static uint8_t  s_rot_stable     = 0;
static uint32_t s_rot_change_tms = 0;

/* ========= helpers ========= */
static inline uint8_t pin_low(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1u : 0u;
}

/* Rohcode anhand einzelner Eingänge (aktive LOW Pull-Ups) */
static uint8_t rotary_read_raw(void)
{
    if (pin_low(XHC_ROT1_GPIO_Port, XHC_ROT1_Pin)) return XHC_ROT_X;     /* 0x11 */
    if (pin_low(XHC_ROT2_GPIO_Port, XHC_ROT2_Pin)) return XHC_ROT_Y;     /* 0x12 */
    if (pin_low(XHC_ROT3_GPIO_Port, XHC_ROT3_Pin)) return XHC_ROT_Z;     /* 0x13 */
    if (pin_low(XHC_ROT4_GPIO_Port, XHC_ROT4_Pin)) return XHC_ROT_S;     /* 0x15 */
    if (pin_low(XHC_ROT5_GPIO_Port, XHC_ROT5_Pin)) return XHC_ROT_F;     /* 0x14 */
    if (pin_low(XHC_ROT6_GPIO_Port, XHC_ROT6_Pin)) return XHC_ROT_PROC;  /* 0x18 */
    return XHC_ROT_OFF;
}

/* ========= Public API ========= */

void IOInputs_Init(void)
{
    /* Encoder starten (Timer in CubeMX als Encoder konfiguriert) */
    HAL_TIM_Encoder_Start(&XHC_ENC_TIM, TIM_CHANNEL_ALL);

    /* Startwert merken */
    s_enc_prev_cnt = __HAL_TIM_GET_COUNTER(&XHC_ENC_TIM);
    s_enc_accum    = 0;

    /* Rotary State init */
    s_rot_last_raw   = rotary_read_raw();
    s_rot_stable     = s_rot_last_raw;
    s_rot_change_tms = HAL_GetTick();
}

void IOInputs_Deinit(void)
{
    HAL_TIM_Encoder_Stop(&XHC_ENC_TIM, TIM_CHANNEL_ALL);
}

/* Optional von außen aufrufbar, um Zähler und Accum zu synchronisieren */
void io_encoder_sync_counter(void)
{
    s_enc_prev_cnt = __HAL_TIM_GET_COUNTER(&XHC_ENC_TIM);
    s_enc_accum    = 0;
}

/* Liefert +- Rastungen seit letztem Aufruf (Wrap-around sicher)
   OFF: nichts puffern → sofort verwerfen und 0 liefern */
int16_t IOInputs_EncoderReadDetents(void)
{
    /* Zählerstand lesen */
    uint16_t cur = __HAL_TIM_GET_COUNTER(&XHC_ENC_TIM);

    /* Rotary prüfen → OFF? dann alles verwerfen und 0 liefern */
    uint8_t mode = IOInputs_RotaryReadCode();  // 0x00 = OFF
    if (mode == XHC_ROT_OFF) {
        s_enc_prev_cnt = cur;  /* Referenz nachziehen */
        s_enc_accum    = 0;    /* Puffer leeren */
        return 0;
    }

    /* Diff wrap-sicher bilden & Referenz updaten */
    int16_t diff = (int16_t)(cur - s_enc_prev_cnt);
    s_enc_prev_cnt = cur;

    /* Flanken akkumulieren → Rastungen extrahieren */
    s_enc_accum += diff;
    int16_t detents = (int16_t)(s_enc_accum / XHC_ENC_PULSES_PER_DETENT);
    s_enc_accum -= (int32_t)detents * XHC_ENC_PULSES_PER_DETENT;

    return detents;  // kann negativ sein
}

/* Entprellter Rotary-Code: Änderung erst nach XHC_ROT_DEBOUNCE_MS übernehmen */
uint8_t IOInputs_RotaryReadCode(void)
{
    uint8_t  raw = rotary_read_raw();
    uint32_t now = HAL_GetTick();

    if (raw != s_rot_last_raw) {
        s_rot_last_raw   = raw;
        s_rot_change_tms = now;       /* Debounce neu starten */
        return s_rot_stable;          /* bis Debounce fertig, alten Wert liefern */
    }

    if ((now - s_rot_change_tms) >= XHC_ROT_DEBOUNCE_MS) {
        s_rot_stable = raw;           /* stabil → übernehmen */
    }
    return s_rot_stable;
}
