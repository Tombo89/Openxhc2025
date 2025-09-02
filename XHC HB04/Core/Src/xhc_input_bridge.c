/*
 * xhc_input_bridge.c
 *
 *  Created on: Sep 2, 2025
 *      Author: Thomas Weckmann
 */

#include "xhc_input_bridge.h"
#include "io_inputs.h"           // io_encoder_read_detents(), io_rotary_read()
#include "usbd_custom_hid_if.h"  // USBD_CUSTOM_HID_SendReport
#include "usbd_def.h"
#include <string.h>

/* ======= Descriptor-Annahme (anpassbar) ======= */
#ifndef XHC_RPT_IN_ID
#define XHC_RPT_IN_ID           0x04   /* deine Input-Report-ID */
#endif
#ifndef XHC_RPT_IN_LEN
#define XHC_RPT_IN_LEN          6      /* 6 Bytes insgesamt (inkl. ID) */
#endif
/* Byte-Positionen (0-basiert) innerhalb des Report-Puffers */
#ifndef XHC_POS_ID
#define XHC_POS_ID              0
#endif
#ifndef XHC_POS_BTN1
#define XHC_POS_BTN1            1
#endif
#ifndef XHC_POS_BTN2
#define XHC_POS_BTN2            2
#endif
#ifndef XHC_POS_WHEELMODE
#define XHC_POS_WHEELMODE       3
#endif
#ifndef XHC_POS_WHEEL
#define XHC_POS_WHEEL           4
#endif
#ifndef XHC_POS_XOR
#define XHC_POS_XOR             5
#endif

/* Rotary Codes wie im Altprojekt / PDF */
#define ROT_CODE_X   0x11
#define ROT_CODE_Y   0x12
#define ROT_CODE_Z   0x13
#define ROT_CODE_F   0x14
#define ROT_CODE_S   0x15
#define ROT_CODE_A   0x18

/* Verhalten: bei OFF nichts senden (Pending bleibt gepuffert) */
#ifndef XHC_IGNORE_WHEEL_WHEN_OFF
#define XHC_IGNORE_WHEEL_WHEN_OFF  1
#endif

/* Coalescing: max. Rastungen pro Report (passt in int8_t) */
#ifndef XHC_WHEEL_MAX_STEP
#define XHC_WHEEL_MAX_STEP  7
#endif

/* Optionales Salz für XOR (falls dein Host das erwartet) */
#ifndef XHC_XOR_SALT
#define XHC_XOR_SALT  0x00
#endif

extern USBD_HandleTypeDef hUsbDeviceFS;

static volatile int16_t s_pending_detents = 0;

static inline int8_t clamp_i8(int32_t v, int32_t lim)
{
    if (v >  lim) return (int8_t)lim;
    if (v < -lim) return (int8_t)(-lim);
    return (int8_t)v;
}

void XHC_InputBridge_Init(void)
{
    s_pending_detents = 0;
}

/* baut Report 0x04 und sendet ihn */
static uint8_t build_and_send_report(uint8_t wheel_mode, int8_t wheel_delta,
                                     uint8_t btn1, uint8_t btn2)
{
    uint8_t rpt[XHC_RPT_IN_LEN];
    if (sizeof(rpt) < 6) return 1; /* Schutz */

    memset(rpt, 0, sizeof(rpt));
    rpt[XHC_POS_ID]        = XHC_RPT_IN_ID;
    rpt[XHC_POS_BTN1]      = btn1;
    rpt[XHC_POS_BTN2]      = btn2;
    rpt[XHC_POS_WHEELMODE] = wheel_mode;
    rpt[XHC_POS_WHEEL]     = (uint8_t)wheel_delta;

    /* einfache XOR-Prüfsumme über die ersten 5 Bytes + Salz */
    uint8_t x = XHC_XOR_SALT;
    for (uint8_t i = 0; i < XHC_POS_XOR; ++i) x ^= rpt[i];
    rpt[XHC_POS_XOR] = x;

    /* Senden – Cube Custom HID erwartet ID im Payload, Länge = 6 */
    USBD_StatusTypeDef st = USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, rpt, sizeof(rpt));
    return (st == USBD_OK) ? 0 : 1;
}

void XHC_InputBridge_Tick(void)
{
    /* 1) Encoder-Rastungen einsammeln */
    int16_t d = io_encoder_read_detents();  /* ±N pro Tick */
    if (d) {
        int32_t acc = (int32_t)s_pending_detents + d;
        if (acc >  32767) acc =  32767;
        if (acc < -32768) acc = -32768;
        s_pending_detents = (int16_t)acc;
    }

    if (s_pending_detents == 0) return;

    /* 2) Rotary-Mode holen (0=OFF, sonst 0x11..0x18) */
    uint8_t rot = io_rotary_read();

#if XHC_IGNORE_WHEEL_WHEN_OFF
    if (rot == 0) {
        /* OFF → nichts schicken; Pending bleibt bestehen */
        return;
    }
#endif

    /* 3) In Portionen senden (±7 pro Report) */
    int8_t burst = clamp_i8(s_pending_detents, XHC_WHEEL_MAX_STEP);

    /* Buttons aktuell (falls du Taster später einliest, hier rein) */
    uint8_t btn1 = 0x00;
    uint8_t btn2 = 0x00;

    if (build_and_send_report(rot, burst, btn1, btn2) == 0) {
        s_pending_detents -= burst;  /* nur abbuchen, wenn gesendet */
    }
}

