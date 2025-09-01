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

#define UI_MIN_PERIOD_MS  150u
#define FRAME_HOLD_MS     700u

/* ==== Frame-Struktur (gepackt) ==== */
#pragma pack(push,1)
typedef struct { uint16_t p_int; uint16_t p_frac; } xhc_pos_t;
typedef struct {
    uint16_t magic; uint8_t day;
    xhc_pos_t pos[6];   /* Xw,Yw,Zw, Xm,Ym,Zm  (wie in deinem Alt-Code) */
    uint16_t feedrate_ovr, sspeed_ovr, feedrate, sspeed;
    uint8_t  step_mul, state;
} whb04_out_data_t;
#pragma pack(pop)

/* ==== Assembler für 37B-Frame ==== */
static uint8_t  asm_buf[XHC_FRAME_SIZE];
static uint8_t  asm_len = 0;
static inline void asm_reset(void){ asm_len = 0; }
static void asm_feed7(const uint8_t *p7)
{
    if (asm_len == 0){
        /* nur starten, wenn Chunk mit FE FD beginnt */
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

static void feat06_to_text(const uint8_t *p7, char *out, uint16_t outsz)
{
    /* p7[1..6] = 6 Stellen; MSB = Dezimalpunkt. Führendes '-' als Signum. */
    char tmp[16]; uint8_t dp[6] = {0}; uint8_t sign=0; uint8_t k=0;
    for (int i=1;i<=6 && k<6;i++){
        char c = seg7_to_char(p7[i]);
        if (!sign && c=='-') { sign=1; continue; }
        tmp[k] = c; dp[k] = (p7[i]&0x80)?1:0; k++;
    }
    uint16_t pos=0;
    if (sign && pos+1<outsz) out[pos++]='-';
    for (uint8_t i=0;i<k && pos+1<outsz;i++){
        out[pos++]=tmp[i];
        if (dp[i] && pos+1<outsz) out[pos++]='.';
    }
    if (pos==0) out[pos++]='0';
    out[pos]=0;
}

/* ==== UI-State ==== */
static uint8_t  live_payload[7];
static uint8_t  have_live = 0;

static uint8_t  frame_cache[XHC_FRAME_SIZE];
static uint8_t  have_frame = 0;
static uint32_t frame_t    = 0;

static uint8_t  shown_source = 0; /* 0=nix, 1=LIVE, 2=FRAME */
static uint32_t t_last_draw  = 0;

void RenderScreen_Init(void)
{
    XHC_Display_Init();
    XHC_Display_SetHeader("HB04 ready");
    XHC_Display_SetLine(1, "");
    XHC_Display_SetLine(2, "");
    XHC_Display_SetLine(3, "");
}

/* Eine Zeile für zwei Werte: „LblA: <valA>   LblB: <valB>“ (kompakt) */
static void line_two_vals(const char* la, uint16_t iA, uint16_t fA,
                          const char* lb, uint16_t iB, uint16_t fB,
                          char *out, int outsz)
{
    char a[24], b[24];
    /* kurze, kompakte Darstellung: ipadd=4, fpadd=3 */
    xhc2string(iA, fA, 4, 3, a);
    xhc2string(iB, fB, 4, 3, b);
    /* knapp halten (ST7735 160px ~ 22 Zeichen in Font_7x10) */
    // Beispiel: "Xw:  123.456  Yw:   12.345"
    // (ohne printf-heavy formatting)
    int p=0; #define PUT(c) do{ if(p<outsz-1){ out[p++]=(c);} }while(0)
    for(const char* s=la; *s; ++s) PUT(*s); PUT(' '); PUT(':'); PUT(' ');
    for(const char* s=a; *s; ++s) PUT(*s);
    PUT(' '); PUT(' ');  /* kleiner Abstand */
    for(const char* s=lb; *s; ++s) PUT(*s); PUT(' '); PUT(':'); PUT(' ');
    for(const char* s=b; *s; ++s) PUT(*s);
    out[p]=0;
}

void RenderScreen(void)
{
    /* 1) Reports einsammeln */
    uint8_t rx[64]; uint16_t n=sizeof(rx);
    while (XHC_RX_TryPop(rx, &n)) {
        if (n>=8 && rx[0]==XHC_FEAT_ID){
            memcpy(live_payload, &rx[1], 7);
            have_live = 1;
            asm_feed7(&rx[1]);
        }
        n=sizeof(rx);
    }

    uint32_t now = HAL_GetTick();

    /* 2) 37B Frame fertig? -> cachen & „halten“ */
    if (asm_len >= XHC_FRAME_SIZE){
        memcpy(frame_cache, asm_buf, XHC_FRAME_SIZE);
        have_frame = 1; frame_t = now; asm_reset();
    }

    /* 3) Quelle wählen (Frame bevorzugt für FRAME_HOLD_MS) */
    uint8_t want = 0;
    if (have_frame && (now - frame_t) <= FRAME_HOLD_MS) want = 2;
    else if (have_live) want = 1;
    else return;

    /* 4) Rate-Limit nur anwenden, wenn Quelle gleich bleibt */
    if (want == shown_source && (now - t_last_draw) < UI_MIN_PERIOD_MS) return;

    /* 5) Rendern */
    if (want == 2){
        /* FRAME */
        whb04_out_data_t f; memcpy(&f, frame_cache, sizeof(f));
        if (f.magic != XHC_MAGIC_LE){
            /* ungültig -> zur Live-Anzeige wechseln, wenn vorhanden */
            if (have_live) want = 1; else return;
        } else {
            char head[32];
            /* feste Kopfzeile, springt nicht: */
            int p=0; head[p++]='S'; head[p++]='R'; head[p++]='C'; head[p++]=':'; head[p++]='F'; head[p++]='R'; head[p++]='A'; head[p++]='M'; head[p++]='E';
            head[p++]=' '; head[p++]='D'; head[p++]='a'; head[p++]='y'; head[p++]=':'; head[p++]=' ';
            unsigned d=f.day; if (d>=100) head[p++]='0'+(d/100)%10; if (d>=10) head[p++]='0'+(d/10)%10; head[p++]='0'+(d%10); head[p]=0;

            char l1[48], l2[48], l3[48];
            line_two_vals("Xw", f.pos[0].p_int, f.pos[0].p_frac,
                          "Yw", f.pos[1].p_int, f.pos[1].p_frac, l1, sizeof(l1));
            line_two_vals("Zw", f.pos[2].p_int, f.pos[2].p_frac,
                          "Xm", f.pos[3].p_int, f.pos[3].p_frac, l2, sizeof(l2));
            line_two_vals("Ym", f.pos[4].p_int, f.pos[4].p_frac,
                          "Zm", f.pos[5].p_int, f.pos[5].p_frac, l3, sizeof(l3));

            XHC_Display_SetHeader(head);
            XHC_Display_SetLine(1, l1);
            XHC_Display_SetLine(2, l2);
            XHC_Display_SetLine(3, l3);

            shown_source = 2; t_last_draw = now; return;
        }
    }

    if (want == 1){
        /* LIVE (0x06) */
        char head[] = "SRC:LIVE";
        char num[24]; feat06_to_text(live_payload, num, sizeof(num));

        XHC_Display_SetHeader(head);
        XHC_Display_SetLine(1, num);
        XHC_Display_SetLine(2, "");
        XHC_Display_SetLine(3, "");

        shown_source = 1; t_last_draw = now; return;
    }
}

