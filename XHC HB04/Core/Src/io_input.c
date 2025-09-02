/*
 * io_input.c
 *
 *  Created on: Sep 2, 2025
 *      Author: Thomas Weckmann
 */

#include "io_inputs.h"
#include "main.h"
#include "stm32f1xx_hal_tim.h"    // optional, aber klarer
extern TIM_HandleTypeDef htim2;   // kommt aus main.c

/* ========= Pin-/Timer-Mapping (ANPASSEN, falls nötig) =========
 * Wenn du in CubeMX Pins mit Labels versehen hast, kannst du hier
 * auf diese Labels mappen. Standardmäßig nutzen wir die Alt-Belegung.
 */

/* --- Rotary 6-Pos Schalter --- */
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


/* --- Encoder-Timer (Handle aus tim.c) --- */
#ifndef XHC_ENC_TIM
  #define XHC_ENC_TIM htim2 /* ändere auf htim3/htim4 etc., falls anderer Timer */
#endif

/* --- Encoder-Detent-Auflösung (4 Flanken = 1 Rastung) --- */
#ifndef XHC_ENC_PULSES_PER_DETENT
  #define XHC_ENC_PULSES_PER_DETENT 4
#endif

/* --- Entprell-Zeit Rotary (ms) --- */
#ifndef XHC_ROT_DEBOUNCE_MS
  #define XHC_ROT_DEBOUNCE_MS 5u
#endif

/* ========= interne Stati ========= */
static uint16_t s_enc_prev_cnt = 0;
static int32_t  s_enc_accum    = 0;

static uint8_t  s_rot_last_raw   = 0;     /* zuletzt gelesener Rohcode */
static uint8_t  s_rot_stable     = 0;     /* entprellt stabiler Code */
static uint32_t s_rot_change_tms = 0;     /* Start der Debounce-Phase */

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
    /* Encoder starten (Timer muss in CubeMX als Encoder konfiguriert sein) */
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

/* Liefert +- Rastungen seit letztem Aufruf (Wrap-around sicher) */
int16_t IOInputs_EncoderReadDetents(void)
{
    uint16_t cur = __HAL_TIM_GET_COUNTER(&XHC_ENC_TIM);
    int16_t diff = (int16_t)(cur - s_enc_prev_cnt); /* signed: Wrap korrekt */
    s_enc_prev_cnt = cur;

    s_enc_accum += diff;
    int16_t det  = (int16_t)(s_enc_accum / XHC_ENC_PULSES_PER_DETENT);
    s_enc_accum -= (int32_t)det * XHC_ENC_PULSES_PER_DETENT;

    return det;
}

/* Entprellter Rotary-Code:
 * Änderung wird erst nach XHC_ROT_DEBOUNCE_MS übernommen,
 * wenn der Rohcode stabil bleibt.
 */
uint8_t IOInputs_RotaryReadCode(void)
{
    uint8_t raw = rotary_read_raw();
    uint32_t now = HAL_GetTick();

    if (raw != s_rot_last_raw) {
        s_rot_last_raw   = raw;
        s_rot_change_tms = now; /* Debounce neu starten */
        return s_rot_stable;    /* bis Debounce durch ist, alten Wert liefern */
    }

    if ((now - s_rot_change_tms) >= XHC_ROT_DEBOUNCE_MS) {
        /* stabil genug -> übernehmen */
        s_rot_stable = raw;
    }
    return s_rot_stable;
}

