#include "xhc_input_bridge.h"
#include "io_inputs.h"              // io_encoder_read_detents(), io_rotary_read()
#include "usbd_custom_hid_if.h"     // USBD_CUSTOM_HID_SendReport
#include "usbd_customhid.h"         // CUSTOM_HID_IDLE
#include "usbd_def.h"
#include "io_kbd.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

// Report-Layout: [0]=0x04 (ReportID), [1]=btn1, [2]=btn2, [3]=wheel_mode, [4]=wheel, [5]=xor_day
static uint8_t pkt[6];
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
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return;

    // --- Buttons lesen (NEU) ---
    uint8_t b1=0, b2=0;
    KBD_Read(&b1, &b2);          // max. zwei Codes, sonst 0

    // --- Rotary + Encoder ---
    uint8_t wheel_mode = IOInputs_RotaryReadCode();  // 0x11/0x12/...
    int16_t det = IOInputs_EncoderReadDetents();         // -/+ Rastungen seit letztem Aufruf
    if (det > 127) det = 127;
    if (det < -128) det = -128;

    // --- Paket füllen ---
    pkt[0] = 0x04;               // Report ID
    pkt[1] = b1;                 // btn_1
    pkt[2] = b2;                 // btn_2
    pkt[3] = wheel_mode;         // wheel_mode
    pkt[4] = (uint8_t)(int8_t)det; // wheel (signed)
    pkt[5] = (uint8_t)(s_day ^ b1); // xor_day (s_day vorher via SetDay aktualisieren)

    // --- Senden (nur wenn vorheriger Transfer fertig) ---
    if (USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, pkt, sizeof(pkt)) == USBD_OK) {
        // ok
    }
}
