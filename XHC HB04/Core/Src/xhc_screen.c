/*
 * xhc_screen.c
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#include <string.h>
#include <stdint.h>
#include "xhc_screen.h"
#include "xhc_display.h"
#include "xhc_format.h"
#include "usbd_custom_hid_if.h"   // XHC_RX_TryPop
#include "stm32f1xx_hal.h"

/* ==== Protokoll-Konstanten ==== */
#define XHC_FEAT_ID       0x06u
#define XHC_CHUNK_SIZE    7u
#define XHC_FRAME_SIZE    37u
#define XHC_MAGIC_LE      0xFDFE

#define UI_MIN_PERIOD_MS  150u   /* max. ~6–7 Hz neu rendern */
#define FRAME_HOLD_MS     700u   /* nach einem Frame kurz „Frame bevorzugen“ */

/* ==== Frame-Struktur (gepackt) ==== */
#pragma pack(push,1)
typedef struct { uint16_t p_int; uint16_t p_frac; } xhc_pos_t;
typedef struct {
    uint16_t magic; uint8_t day;
    xhc_pos_t pos[6];   /* [0..2]=Work X/Y/Z, [3..5]=Machine X/Y/Z */
    uint16_t feedrate_ovr, sspeed_ovr, feedrate, sspeed;
    uint8_t  step_mul, state;
} whb04_out_data_t;
#pragma pack(pop)

/* ==== Assembler für 37B-Frame (Feature 0x06 in 7-Byte-Chunks) ==== */
static uint8_t  asm_buf[XHC_FRAME_SIZE];
static uint8_t  asm_len = 0;

static inline void asm_reset(void){ asm_len = 0; }

static void asm_feed7(const uint8_t *p7)
{
    if (asm_len == 0){
        /* Start nur, wenn Chunk mit FE FD beginnt */
        if (p7[0]==0xFE && p7[1]==0xFD){
            memcpy(asm_buf, p7, XHC_CHUNK_SIZE);
            asm_len = XHC_CHUNK_SIZE;
        }
        return;
    }
    uint8_t room = XHC_FRAME_SIZE - asm_len;
    uint8_t cp   = (room >= XHC_CHUNK_SIZE) ? XHC_CHUNK_SIZE : room;
    memcpy(&asm_buf[asm_len], p7, cp);
    asm_len += cp;
}

/* ==== Live 0x06 Dekoder (7-Segment → Text) ==== */
static char seg7_to_char(uint8_t b){
    uint8_t s = b & 0x7F;
    switch(s){
        case 0x3F: return '0'; case 0x06: return '1'; case 0x5B: return '2';
        case 0x4F: return '3'; case 0x66: return '4'; case 0x6D: return '5';
        case 0x7D: return '6'; case 0x07: return '7'; case 0x7F: return '8';
        case 0x6F: return '9'; case 0x40: return '-'; default: return ' ';
    }
}

/* p7[0..6] = 7 Byte Payload; i. d. R. sind p7[1..6] die 6 Zeichen; MSB = Dezimalpunkt */
static void feat06_to_text(const uint8_t *p7, char *out, uint16_t outsz)
{
    /* 7-Segment in Ziffern + Dezimalpunkt-Lage übersetzen */
    char digs[6]; uint8_t dp[6]={0}; uint8_t k=0, neg=0;
    for (int i=1; i<=6 && k<6; i++){
        char c;
        switch (p7[i] & 0x7F){
            case 0x3F: c='0'; break; case 0x06: c='1'; break; case 0x5B: c='2'; break;
            case 0x4F: c='3'; break; case 0x66: c='4'; break; case 0x6D: c='5'; break;
            case 0x7D: c='6'; break; case 0x07: c='7'; break; case 0x7F: c='8'; break;
            case 0x6F: c='9'; break; case 0x40: c='-'; break; default: c=' '; break;
        }
        if (!neg && c=='-') { neg=1; continue; }
        digs[k] = c;
        dp[k] = (p7[i] & 0x80) ? 1 : 0;
        k++;
    }
    /* int-/frac-Listen aufbauen anhand dp-Flags ('.' nach der Ziffer mit gesetztem dp) */
    char ibuf[6], fbuf[6]; int ilen=0, flen=0;
    uint8_t after_dot = 0;
    for (uint8_t i=0; i<k; i++){
        if (!after_dot) {
            ibuf[ilen++] = (digs[i] >= '0' && digs[i] <= '9') ? digs[i] : '0';
            if (dp[i]) after_dot = 1;
        } else {
            fbuf[flen++] = (digs[i] >= '0' && digs[i] <= '9') ? digs[i] : '0';
        }
    }
    if (ilen == 0) { ibuf[ilen++]='0'; }
    /* auf 4 Integer- und 4 Fraction-Ziffern normalisieren */
    char i4[4], f4[4];
    // Integer rechtsbündig
    for (int i=0; i<4; ++i){
        int src = ilen - 4 + i;
        i4[i] = (src >= 0) ? ibuf[src] : ' ';
    }
    // Fraction auf 4, fehlende mit '0' auffüllen
    for (int i=0; i<4; ++i){
        f4[i] = (i < flen) ? fbuf[i] : '0';
    }
    // Ausgabe exakt 10 Zeichen: S I I I I . F F F F
    int p=0;
    if (p < (int)outsz-1) out[p++] = neg ? '-' : ' ';
    for (int i=0; i<4 && p < (int)outsz-1; ++i) out[p++] = i4[i];
    if (p < (int)outsz-1) out[p++]='.';
    for (int i=0; i<4 && p < (int)outsz-1; ++i) out[p++] = f4[i];
    out[p]=0;
}

/* ==== UI-State ==== */
static uint8_t  live_payload[7];
static uint8_t  have_live = 0;

static uint8_t  frame_cache[XHC_FRAME_SIZE];
static uint8_t  have_frame = 0;
static uint32_t frame_t    = 0;

static uint8_t  shown_source = 0; /* 0=nix, 1=LIVE, 2=FRAME */
static uint32_t t_last_draw  = 0;

/* eine Zeile „Lbl: <wert>“ kompakt ohne printf aufbauen (Signum via xhc2string) */
static void one_val_line(const char* lbl, uint16_t i, uint16_t f, char *out, int outsz)
{
    char v[16];                        // 10 + '\0' reicht
    xhc2string_align10(i, f, v);       // immer "[-]xxxx.xxxx"

    int p = 0;
    /* Label */
    const char* s = lbl;
    while (*s && p < outsz-1) out[p++] = *s++;
    if (p < outsz-1) out[p++]=':';
    if (p < outsz-1) out[p++]=' ';
    /* Wert */
    s = v;
    while (*s && p < outsz-1) out[p++] = *s++;
    out[p] = 0;
}

void RenderScreen_Init(void)
{
    XHC_Display_Init();
    XHC_Display_SetHeader("HB04 ready");
    XHC_Display_SetLine(1, "");
    XHC_Display_SetLine(2, "");
    XHC_Display_SetLine(3, "");
    XHC_Display_SetLine(4, "");
    XHC_Display_SetLine(5, "");
    XHC_Display_SetLine(6, "");
}

void RenderScreen(void)
{
    /* 1) Reports einsammeln */
    uint8_t rx[64]; uint16_t n=sizeof(rx);
    while (XHC_RX_TryPop(rx, &n)) {
        if (n>=8 && rx[0]==XHC_FEAT_ID){
            memcpy(live_payload, &rx[1], 7);
            have_live = 1;
            asm_feed7(&rx[1]);   /* 37B-Assembler füttern */
        }
        n=sizeof(rx);
    }

    uint32_t now = HAL_GetTick();

    /* 2) 37B Frame fertig? -> cachen & „halten“ */
    if (asm_len >= XHC_FRAME_SIZE){
        memcpy(frame_cache, asm_buf, XHC_FRAME_SIZE);
        have_frame = 1; frame_t = now; asm_reset();
    }

    /* 3) Quelle wählen (Frame bevorzugen, wenn frisch) */
    uint8_t want = 0;
    if (have_frame && (now - frame_t) <= FRAME_HOLD_MS) want = 2;
    else if (have_live) want = 1;
    else return;

    /* 4) Rate-Limit, aber nur wenn Quelle gleich bleibt */
    if (want == shown_source && (now - t_last_draw) < UI_MIN_PERIOD_MS) return;

    /* 5) Rendern */
    if (want == 2){
        /* ===== FRAME (37B) ===== */
        whb04_out_data_t f; memcpy(&f, frame_cache, sizeof(f));
        if (f.magic != XHC_MAGIC_LE){
            /* invalider Frame → auf Live wechseln, wenn vorhanden */
            if (!have_live) return;
            want = 1;
        } else {
            /* fester Header: "SRC:FRAME Day:NN" */
            char head[32];
            int p=0; const char *fix="SRC:FRAME Day:";
            while (*fix && p<(int)sizeof(head)-1) head[p++]=*fix++;
            unsigned d=f.day;
            if (d>=100 && p<(int)sizeof(head)-1) head[p++]='0'+(d/100)%10;
            if (d>=10  && p<(int)sizeof(head)-1) head[p++]='0'+(d/10)%10;
            if (p<(int)sizeof(head)-1)          head[p++]='0'+(d%10);
            head[p]=0;

            char l1[48], l2[48], l3[48], l4[48], l5[48], l6[48];

            /* Vertikal: Work dann Machine */
            one_val_line("Xw", f.pos[0].p_int, f.pos[0].p_frac, l1, sizeof(l1));
            one_val_line("Yw", f.pos[1].p_int, f.pos[1].p_frac, l2, sizeof(l2));
            one_val_line("Zw", f.pos[2].p_int, f.pos[2].p_frac, l3, sizeof(l3));
            one_val_line("Xm", f.pos[3].p_int, f.pos[3].p_frac, l4, sizeof(l4));
            one_val_line("Ym", f.pos[4].p_int, f.pos[4].p_frac, l5, sizeof(l5));
            one_val_line("Zm", f.pos[5].p_int, f.pos[5].p_frac, l6, sizeof(l6));

            XHC_Display_SetHeader(head);
            XHC_Display_SetLine(1, l1);
            XHC_Display_SetLine(2, l2);
            XHC_Display_SetLine(3, l3);
            XHC_Display_SetLine(4, l4);
            XHC_Display_SetLine(5, l5);
            XHC_Display_SetLine(6, l6);

            shown_source = 2; t_last_draw = now; return;
        }
    }

    /* ===== LIVE (0x06) ===== */
    if (want == 1){
        char head[] = "SRC:LIVE";
        char num[24]; feat06_to_text(live_payload, num, sizeof(num));

        XHC_Display_SetHeader(head);
        XHC_Display_SetLine(1, num);
        XHC_Display_SetLine(2, "");
        XHC_Display_SetLine(3, "");
        XHC_Display_SetLine(4, "");
        XHC_Display_SetLine(5, "");
        XHC_Display_SetLine(6, "");

        shown_source = 1; t_last_draw = now; return;
    }
}
