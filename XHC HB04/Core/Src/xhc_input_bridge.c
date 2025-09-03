#include "xhc_input_bridge.h"
#include "io_inputs.h"              // io_encoder_read_detents(), io_rotary_read()
#include "usbd_custom_hid_if.h"     // USBD_CUSTOM_HID_SendReport
#include "usbd_customhid.h"         // CUSTOM_HID_IDLE
#include "usbd_def.h"
#include "io_kbd.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

/* ==== Wheel speed via sliding window (last 80ms) ==== */
#define XHC_WHEEL_WIN_MS   80u   /* Zeitfenster für Geschwindigkeitsmessung */
#define XHC_SEND_MIN_MS     2u   /* min. Abstand zwischen IN-Reports */
#define XHC_HEARTBEAT_MS   20u   /* 0 = aus; sonst periodisch Report schicken */

static uint32_t s_det_times[16];  /* Zeitstempel der letzten Detents (Ringpuffer) */
static uint8_t  s_det_head = 0;   /* nächste Schreibposition (0..15) */
static uint8_t  s_det_count = 0;  /* wie viele Einträge sind gültig (0..16) */
static int8_t   s_last_sign = +1; /* +1 / -1: Richtung der letzten Bewegung */
static uint32_t s_last_send_ms = 0;
static uint8_t  s_last_mode_sent = 0xFF;


#define XHC_WHEEL_REPORT_PERIOD_MS   8u   // Fensterbreite fürs Speed-Sampling
#define XHC_WHEEL_MAX_ABS            10   // laut PDF: 1..10 bzw. -1..-10
#define MAX_DETENTS_PER_RPT  10
#define XHC_WHEEL_FRAME_MS     10u   // Fensterlänge (8–12 ms ist typisch)
#define XHC_WHEEL_MAX_PER_RPT  10    // laut Spez

static int16_t  s_win_count = 0;     // Detents im aktuellen Fenster
static uint32_t s_t_frame   = 0;     // Startzeit des Fensters

static int16_t s_pending_detents = 0;

static uint32_t s_last_rpt_ms     = 0;
static int16_t  s_window_detents  = 0;
static uint8_t  s_btn1_last       = 0;
static uint8_t  s_btn2_last       = 0;


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




static volatile uint8_t s_day = 0;
static volatile int32_t s_wheel_pending = 0;  // sammelt Detents bis zum erfolgreichen Send
static int16_t s_wheel_accum = 0;   // sammelt Detents bis zum erfolgreichen Send

/* älteste Einträge verwerfen, die außerhalb des Fensters liegen */
static inline void wheel_prune_expired(uint32_t now)
{
    while (s_det_count) {
        uint8_t tail = (uint8_t)((s_det_head - s_det_count) & 0x0F);
        if ((now - s_det_times[tail]) <= XHC_WHEEL_WIN_MS) break;
        s_det_count--; /* ältesten Eintrag “abwerfen” */
    }
}

/* n Detents mit Zeitstempel now in den Ring schreiben */
static inline void wheel_push_detents(uint32_t now, uint8_t n)
{
    while (n--) {
        s_det_times[s_det_head] = now;
        s_det_head = (uint8_t)((s_det_head + 1u) & 0x0F);
        if (s_det_count < 16u) s_det_count++;
        /* bei Überlauf ersetzen wir implizit die ältesten – s_det_count bleibt 16 */
    }
}

/* Level 0..10 = wie viele Detents in den letzten XHC_WHEEL_WIN_MS (gecappt) */
static inline uint8_t wheel_level_from_window(void)
{
    uint8_t lvl = s_det_count;
    if (lvl > 10u) lvl = 10u;
    return lvl;
}


/* Du liest Buttons später? Für jetzt 0 lassen. */
static inline void read_buttons(uint8_t* b1, uint8_t* b2){
    *b1 = 0x00; *b2 = 0x00;
}

static inline int8_t sat_s8(int32_t v) {
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
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

    uint32_t now = HAL_GetTick();

    // 1) Neue Rastungen aufsummieren
    int16_t d = IOInputs_EncoderReadDetents();   // kann ± sein
    if (d) s_win_count += d;

    // 2) Alle XHC_WHEEL_FRAME_MS einen Report vorbereiten
    if ((uint32_t)(now - s_t_frame) < XHC_WHEEL_FRAME_MS) return;
    s_t_frame = now;

    // Rotary-Mode besorgen (0x00=OFF, sonst 0x11..0x18)
    uint8_t rot = IOInputs_RotaryReadCode();

    // Falls im OFF nichts „nachschieben“ soll:
    if (rot == 0x00) { s_win_count = 0; return; }

    // 3) Nichts zu senden?
    if (s_win_count == 0) return;

    // 4) Betrag auf 1..10 begrenzen, Vorzeichen behalten
    int16_t q = s_win_count;
    int8_t wheel = 0;
    if (q > 0)       wheel = (q >  XHC_WHEEL_MAX_PER_RPT) ?  XHC_WHEEL_MAX_PER_RPT : (int8_t)q;
    else /* q < 0 */ wheel = (q < -XHC_WHEEL_MAX_PER_RPT) ? -XHC_WHEEL_MAX_PER_RPT : (int8_t)q;

    // 5) Nur senden, wenn USB idle
    USBD_CUSTOM_HID_HandleTypeDef *hhid =
        (USBD_CUSTOM_HID_HandleTypeDef*)hUsbDeviceFS.pClassData;
    if (!hhid || hhid->state != CUSTOM_HID_IDLE) return;

    uint8_t pkt[6];
    pkt[0] = 0x04;                 // Report ID
    pkt[1] = 0x00;                 // btn_1 (vorerst 0)
    pkt[2] = 0x00;                 // btn_2 (vorerst 0)
    pkt[3] = rot;                  // wheel_mode
    pkt[4] = (uint8_t)wheel;       // wheel: −10..+10
    pkt[5] = (uint8_t)(s_day ^ pkt[1]);  // xor_day = day ^ btn_1

    if (USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, pkt, sizeof(pkt)) == USBD_OK) {
        // gesendeten Anteil aus dem Fenster abziehen (Richtung beachten)
        s_win_count -= wheel;  // wheel ist signed
    }
}
