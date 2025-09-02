#include "xhc_input_bridge.h"
#include "io_inputs.h"              // io_encoder_read_detents(), io_rotary_read()
#include "usbd_custom_hid_if.h"     // USBD_CUSTOM_HID_SendReport
#include "usbd_customhid.h"         // CUSTOM_HID_IDLE
#include "usbd_def.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

/* === Report-Layout wie im Altprojekt === */
#define XHC_RPT_IN_ID          0x04
#define XHC_RPT_IN_LEN         6   /* inkl. ID */

/* Felder (0-basiert im Puffer) */
#define POS_ID                 0
#define POS_BTN1               1
#define POS_BTN2               2
#define POS_WHEELMODE          3
#define POS_WHEEL              4
#define POS_XORDAY             5

/* Verhalten: bei OFF (rotary=0) nichts senden */
#define IGNORE_WHEN_OFF        1

/* Ein Report trägt max. so viele Rastungen (passt in int8_t) */
#define MAX_DETENTS_PER_RPT    7

static volatile int16_t s_pending_detents = 0;
static volatile uint8_t s_day = 0;

/* Du liest Buttons später? Für jetzt 0 lassen. */
static inline void read_buttons(uint8_t* b1, uint8_t* b2){
    *b1 = 0x00; *b2 = 0x00;
}

void XHC_InputBridge_Init(void){
    s_pending_detents = 0;
    s_day = 0;
}

void XHC_InputBridge_SetDay(uint8_t day_from_host){
    s_day = day_from_host;
}

static inline int8_t clamp_i8(int32_t v, int32_t lim){
    if (v >  lim) return (int8_t) lim;
    if (v < -lim) return (int8_t)-lim;
    return (int8_t)v;
}

void XHC_InputBridge_Tick(void)
{
    /* 1) Encoder-Rastungen einsammeln */
    int16_t d = IOInputs_EncoderReadDetents();   /* ±N pro Aufruf */
    if (d){
        int32_t acc = (int32_t)s_pending_detents + d;
        if (acc >  32767) acc =  32767;
        if (acc < -32768) acc = -32768;
        s_pending_detents = (int16_t)acc;
    }

    if (s_pending_detents == 0) return;

    /* 2) Rotary-Mode ermitteln (0=OFF, sonst 0x11..0x18) */
    uint8_t rot = IOInputs_RotaryReadCode();
#if IGNORE_WHEN_OFF
    if (rot == 0) return;       /* Achse abgewählt → nichts senden */
#endif

    /* 3) USB-Kanal frei? (sonst BUSY → Report würde verworfen) */
    USBD_CUSTOM_HID_HandleTypeDef *hhid =
        (USBD_CUSTOM_HID_HandleTypeDef*)hUsbDeviceFS.pClassData;
    if (!hhid || hhid->state != CUSTOM_HID_IDLE) return;

    /* 4) Buttons (falls vorhanden) + Δ begrenzen */
    uint8_t btn1, btn2; read_buttons(&btn1, &btn2);
    int8_t burst = clamp_i8(s_pending_detents, MAX_DETENTS_PER_RPT);

    /* 5) Report 0x04 füllen (genau 6 Bytes) */
    uint8_t rpt[XHC_RPT_IN_LEN];
    rpt[POS_ID]        = XHC_RPT_IN_ID;
    rpt[POS_BTN1]      = btn1;
    rpt[POS_BTN2]      = btn2;
    rpt[POS_WHEELMODE] = rot;
    rpt[POS_WHEEL]     = (uint8_t)burst;     /* signed cast gewollt */
    rpt[POS_XORDAY]    = (uint8_t)(s_day ^ btn1);   /* !!! wie im Altcode !!! */

    /* 6) Abschicken */
    if (USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, rpt, sizeof(rpt)) == USBD_OK){
        s_pending_detents -= burst;  /* nur abbuchen, wenn wirklich unterwegs */
        /* hhid->state wird im Stack auf BUSY gesetzt bis IN-Transfer fertig ist */
    }
}
