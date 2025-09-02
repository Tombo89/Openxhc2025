/*
 * xhc_screen.c
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "xhc_screen.h"
#include "xhc_format.h"
#include "usbd_custom_hid_if.h"   // XHC_RX_TryPop
#include "stm32f1xx_hal.h"

/* Für das Zeichnen des Layouts */
#include "ST7735.h"
#include "GFX_FUNCTIONS.h"
#include "fonts.h"   /* für Font_7x10 und deine reesansbold9pt7b */

#include "io_inputs.h"   // liefert io_rotary_read()

/* ==== Farb-/Display-Konstanten ==== */
#ifndef WHITE
#define WHITE 0xFFFF
#endif
#ifndef BLACK
#define BLACK 0x0000
#endif
#ifndef BLUE
#define BLUE  0x001F
#endif


/* ==== UI-Layout (statisch) ==== */

/* Y-Positionen der 6 Werte-Zeilen (in Pixel) */
static const uint16_t s_val_y[6] = { 2, 14, 26, 50, 62, 74 };
/* X-Start der Zahlen (möglichst bei vielfachem von 7, damit 10 Zeichen gut passen) */
static const uint16_t s_val_x     = 64;  /* 9*7=63 → ~64 px ab linker Kante */

/* Labels-Positionen */
static const uint16_t s_wc_mc_x   = 2;
static const uint16_t s_axis_x    = 35;  /* "X:" / "Y:" / "Z:" links von den Zahlen */

/* Divider & Blue-Bar */
static const uint16_t s_div_y     = 42;  /* dünner Strich zwischen WC/MC */
static const uint16_t s_blue_y    = 96;  /* Start der blauen Fußzeile */

/* Einmal-Flag für statischen Aufbau */
static uint8_t s_static_drawn = 0;


#define FONT_LABEL Font_13x13

/* Pixel-Geometrie für Font_7x10 bei den Werten */
#define CHAR_W  7u
#define LINE_H 12u

/* Displaygröße (bei ST7735 oft 160x128 im Querformat) */
#define LCD_W 160u
#define LCD_H 128u

/* ==== Protokoll-Konstanten ==== */
#define XHC_FEAT_ID       0x06u
#define XHC_CHUNK_SIZE    7u
#define XHC_FRAME_SIZE    37u
#define XHC_MAGIC_LE      0xFDFE

#define UI_MIN_PERIOD_MS  120u   /* sanftes Rate-Limit für Updates */
#define FRAME_HOLD_MS     600u   /* nach vollständigem Frame: Quelle kurz halten */

/* Footer-Text-Startpunkte */
#define FOOT_Y_A   (s_blue_y + 2)    /* obere Footer-Zeile */
#define FOOT_Y_B   (s_blue_y + 14)   /* untere Footer-Zeile */
#define FOOT_X_L   2                 /* linker Block */
#define FOOT_X_R   84                /* rechter Block (ca. Mitte + etwas Rand) */

/* Offsets im 37B-Frame (Little Endian) */

#define OFF_FEED_VAL   31  /* uint16_t, mm/min (typisch) */
#define OFF_SPIND_VAL  33  /* uint16_t, RPM (typisch) */

/* ---- zwei Spalten im Footer (nebeneinander) ---- */
#define COL0_X0    2u
#define COL0_X1   (LCD_W/2u - 2u)
#define COL1_X0   (LCD_W/2u + 2u)
#define COL1_X1   (LCD_W - 2u)

/* eine gemeinsame Y-Position für beide Bars, oben bleibt Platz für Rotary/Step */
#define BARS_Y    (s_blue_y + 16u)
#define BAR_H     12u

/* Labelbreite (ein Zeichen, Font_7x10) + kleiner Abstand vor der Bar */
#define BAR_LABEL_W   (CHAR_W)
#define BAR_PAD       3u

/* abgeleitete Bar-Positionen/-Breiten */
#define F_LABEL_X   (COL0_X0)
#define F_BAR_X     (F_LABEL_X + BAR_LABEL_W + BAR_PAD)
#define F_BAR_W     (COL0_X1 - F_BAR_X)

#define S_LABEL_X   (COL1_X0)
#define S_BAR_X     (S_LABEL_X + BAR_LABEL_W + BAR_PAD)
#define S_BAR_W     (COL1_X1 - S_BAR_X)

#define BAR_MIN_PERIOD_MS  40u

/* POS-Text links oben im blauen Balken */
#define POS_X      2u
#define POS_Y      (s_blue_y + 2u)

/* OFF erst anzeigen, wenn der Schalter so lange wirklich "offen" ist */
#define ROT_OFF_DELAY_MS   120u

/* Farben für Füllung */
#ifndef RED
#define RED    0xF800
#endif
#ifndef GREEN
#define GREEN  0x07E0
#endif

/* Falls doppelt vorhanden: nur EIN Offsets-Block behalten! */
#ifndef OFF_FEED_OVR
#define OFF_FEED_OVR   27  /* uint16_t, Hundertstel-% */
#endif
#ifndef OFF_SPIND_OVR
#define OFF_SPIND_OVR  29  /* uint16_t, % */
#endif


/* Farben (du hast BLUE/WHITE/BLACK schon) */
#ifndef CYAN
#define CYAN  0x07FF
#endif
#ifndef YELLOW
#define YELLOW 0xFFE0
#endif



/* Offsets im 37-Byte-Frame (LE) – nur für die Overrides */



/* ---- forward declarations (needed before first use) ---- */
static void DrawBarFrame(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
static void DrawBarValue(uint8_t which,
                         uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         uint16_t pct, uint16_t minp, uint16_t maxp);

static inline uint16_t rd16_le(const uint8_t *buf, uint8_t off) {
    return (uint16_t)(buf[off] | ((uint16_t)buf[off+1] << 8));
}


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

/* ===== POS: X/Y/Z/F/S/A (links oben im Footer) ===== */
static char     s_pos_last[16];
static uint8_t  s_pos_last_len = 0;

static uint8_t  s_rot_last_raw = 0xFF;   /* zuletzt gelesener Rohcode */
static uint32_t s_rot_off_t0    = 0;     /* Startzeitpunkt "OFF offen" */
static uint8_t  s_rot_show_off  = 1;     /* wir starten mit OFF sichtbar */


static void DrawPOS(const char* txt)
{
    uint8_t oldlen = s_pos_last_len;
    uint8_t len    = (uint8_t)strlen(txt);
    if (len > sizeof(s_pos_last)-1) len = sizeof(s_pos_last)-1;

    /* gemeinsame Präfix-Zeichen aktualisieren */
    uint8_t common = (oldlen < len) ? oldlen : len;
    for (uint8_t i=0; i<common; ++i) {
        if (txt[i] != s_pos_last[i]) {
            char s[2] = { txt[i], 0 };
            ST7735_WriteString((uint16_t)(POS_X + i*CHAR_W), POS_Y, s, Font_7x10, WHITE, BLUE);
        }
    }
    /* zusätzliche neue Zeichen */
    for (uint8_t i=common; i<len; ++i) {
        char s[2] = { txt[i], 0 };
        ST7735_WriteString((uint16_t)(POS_X + i*CHAR_W), POS_Y, s, Font_7x10, WHITE, BLUE);
    }
    /* überhängende alte löschen */
    for (uint8_t i=len; i<oldlen; ++i) {
        fillRect((uint16_t)(POS_X + i*CHAR_W), POS_Y, CHAR_W, LINE_H, BLUE);
    }

    memcpy(s_pos_last, txt, len);
    s_pos_last[len] = 0;
    s_pos_last_len  = len;
}

/* Rohcode → Buchstabe; 0 bedeutet OFF/kein Pin */
static char RotaryCodeToLetter(uint8_t code)
{
    switch (code) {
        case 0x11: return 'X';
        case 0x12: return 'Y';
        case 0x13: return 'Z';
        case 0x14: return 'F'; /* Feedrate */
        case 0x15: return 'S'; /* Spindle  */
        case 0x18: return 'A'; /* A/Processing */
        default:   return 0;   /* OFF */
    }
}

/* OFF-Entprellung: OFF erst nach ROT_OFF_DELAY_MS zeigen */
static void UpdatePOS_FromRotary(void)
{
    uint32_t now  = HAL_GetTick();
    uint8_t  raw  = rotary_read();   /* kommt aus io_input.c */
    char     lett = RotaryCodeToLetter(raw);

    if (lett) {
        /* gültige Position – sofort anzeigen */
        char buf[16];  /* "POS: X" */
        buf[0]='P'; buf[1]='O'; buf[2]='S'; buf[3]=':'; buf[4]=' '; buf[5]=lett; buf[6]=0;
        DrawPOS(buf);

        /* OFF-Status zurücksetzen */
        s_rot_off_t0   = 0;
        s_rot_show_off = 0;
    } else {
        /* OFF (kein Pin aktiv) – nur anzeigen, wenn stabil lange genug */
        if (s_rot_off_t0 == 0) {
            s_rot_off_t0 = now;   /* Start des OFF-Fensters */
        }
        if (!s_rot_show_off && (now - s_rot_off_t0) >= ROT_OFF_DELAY_MS) {
            DrawPOS("POS: OFF");
            s_rot_show_off = 1;
        }
    }

    s_rot_last_raw = raw;
}

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

/* ==== Live 0x06 Dekoder (7-Segment → ausgerichteter Text) ==== */
static char seg7_to_char(uint8_t b){
    uint8_t s = b & 0x7F;
    switch(s){
        case 0x3F: return '0'; case 0x06: return '1'; case 0x5B: return '2';
        case 0x4F: return '3'; case 0x66: return '4'; case 0x6D: return '5';
        case 0x7D: return '6'; case 0x07: return '7'; case 0x7F: return '8';
        case 0x6F: return '9'; case 0x40: return '-'; default: return ' ';
    }
}

/* Live-Wert in genau 10 Zeichen normalisieren: "[-]xxxx.xxxx" (Dezimalpunkte fluchten) */
static void feat06_to_text_align10(const uint8_t *p7, char *out, uint16_t outsz)
{
    /* 7-Segment in Ziffern + Dezimalpunkt-Lage übersetzen */
    char digs[6]; uint8_t dp[6]={0}; uint8_t k=0, neg=0;
    for (int i=1; i<=6 && k<6; i++){
        char c = seg7_to_char(p7[i]);
        if (!neg && c=='-') { neg=1; continue; }
        digs[k] = c;
        dp[k] = (p7[i] & 0x80) ? 1 : 0;
        k++;
    }
    /* int-/frac-Listen anhand dp-Flags ('.' nach der Ziffer mit gesetztem dp) */
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
    for (int i=0; i<4; ++i){
        int src = ilen - 4 + i;
        i4[i] = (src >= 0) ? ibuf[src] : ' ';
    }
    for (int i=0; i<4; ++i){
        f4[i] = (i < flen) ? fbuf[i] : '0';
    }
    /* Ausgabe exakt 10 Zeichen: S I I I I . F F F F */
    int p=0;
    if (p < (int)outsz-1) out[p++] = neg ? '-' : ' ';
    for (int i=0; i<4 && p < (int)outsz-1; ++i) out[p++] = i4[i];
    if (p < (int)outsz-1) out[p++]='.';
    for (int i=0; i<4 && p < (int)outsz-1; ++i) out[p++] = f4[i];
    out[p]=0;
}



/* kleine Writer */
static inline void PUT_STR(uint16_t x, uint16_t y, const char* s,
                           const FontDef font, uint16_t fg, uint16_t bg)
{
    ST7735_WriteString(x, y, s, font, fg, bg);
}



/* statisches Layout genau einmal zeichnen */
static void Draw_Static_Layout_Once(void)
{
    if (s_static_drawn) return;
    s_static_drawn = 1;

    /* Hintergrund + blaue Leiste unten */
    fillScreen(WHITE);  // ggf. schon in main gemacht; auskommentiert, wenn unerwünscht
    fillRect(0, s_blue_y, LCD_W, (uint16_t)(LCD_H - s_blue_y), BLUE);

    /* WC/MC + Achsen-Labels */
    PUT_STR(s_wc_mc_x, s_val_y[0], "WC", FONT_LABEL, BLACK, WHITE);
    PUT_STR(s_axis_x,  s_val_y[0], "X:", FONT_LABEL, BLACK, WHITE);
    PUT_STR(s_axis_x,  s_val_y[1], "Y:", FONT_LABEL, BLACK, WHITE);
    PUT_STR(s_axis_x,  s_val_y[2], "Z:", FONT_LABEL, BLACK, WHITE);

    /* Divider */
    fillRect(0, s_div_y, LCD_W, 1, BLACK);

    PUT_STR(s_wc_mc_x, s_val_y[3], "MC", FONT_LABEL, BLACK, WHITE);
    PUT_STR(s_axis_x,  s_val_y[3], "X:", FONT_LABEL, BLACK, WHITE);
    PUT_STR(s_axis_x,  s_val_y[4], "Y:", FONT_LABEL, BLACK, WHITE);
    PUT_STR(s_axis_x,  s_val_y[5], "Z:", FONT_LABEL, BLACK, WHITE);

    /* Progressbar-Labels + Rahmen (einmalig) */
    ST7735_WriteString(F_LABEL_X, BARS_Y, "F", Font_7x10, WHITE, BLUE);
    ST7735_WriteString(S_LABEL_X, BARS_Y, "S", Font_7x10, WHITE, BLUE);

    DrawBarFrame(F_BAR_X, BARS_Y, F_BAR_W, BAR_H);
    DrawBarFrame(S_BAR_X, BARS_Y, S_BAR_W, BAR_H);

    /* POS initial anzeigen */
    DrawPOS("POS: OFF");
    s_rot_show_off = 1;
    s_rot_off_t0   = HAL_GetTick();
}

/* ==== Werte-Zeichnen mit minimalem Redraw (nur Änderungen) ==== */
static char s_last_val[6][12];  /* 10 Zeichen + 0, etwas Reserve */
static uint8_t s_last_len[6];   /* jeweils 10 */

/* ==== Footer (blauer Streifen) – 4 Text-Slots: 0=F% 1=S% 2=F 3=S ==== */
static char s_last_bot[4][16];     /* jeder Slot bis ~15 Zeichen */
static uint8_t s_last_bot_len[4];


static inline void DrawFooterText(uint8_t slot, uint16_t x, uint16_t y, const char* txt)
{
    if (slot > 3) return;

    uint8_t oldlen = s_last_bot_len[slot];
    uint8_t len    = (uint8_t)strlen(txt);
    if (len > 15) len = 15;

    /* 1) Gemeinsame Präfix-Länge diffbasiert (kein Clear nötig) */
    uint8_t common = (oldlen < len) ? oldlen : len;
    for (uint8_t i = 0; i < common; ++i){
        char nc = txt[i], oc = s_last_bot[slot][i];
        if (nc != oc){
            char s[2] = { nc, 0 };
            ST7735_WriteString((uint16_t)(x + i*CHAR_W), y, s, Font_7x10, WHITE, BLUE);
        }
    }

    /* 2) Neue zusätzliche Zeichen schreiben (falls länger geworden) */
    for (uint8_t i = common; i < len; ++i){
        char s[2] = { txt[i], 0 };
        ST7735_WriteString((uint16_t)(x + i*CHAR_W), y, s, Font_7x10, WHITE, BLUE);
    }

    /* 3) Überhängende alte Zeichen entfernen (falls kürzer geworden) */
    for (uint8_t i = len; i < oldlen; ++i){
        /* Zelle gezielt löschen */
        fillRect((uint16_t)(x + i*CHAR_W), y, CHAR_W, LINE_H, BLUE);
    }

    /* Cache aktualisieren */
    memcpy(s_last_bot[slot], txt, len);
    s_last_bot[slot][len] = 0;
    s_last_bot_len[slot]  = len;
}

/* letzter angezeigter Prozentwert je Bar (zum Skippen unveränderter Frames) */
static uint16_t s_last_bar_val[2] = { 0xFFFF, 0xFFFF };


/* Rahmen einer Bar einmalig zeichnen (blauer Hintergrund ist schon da) */
static void DrawBarFrame(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    /* Bar-Outline */
    drawRect(x, y, w, h, WHITE);
    /* Center-Marker (100%) */
    uint16_t cx = (uint16_t)(x + w/2u);
    drawFastVLine(cx, (int16_t)(y+1), (int16_t)(h-2), WHITE);
}

/* Bar-Inhalt neu zeichnen (nur bei Wertänderung).
   min/max: Prozentbereiche (z.B. F:0..250, S:50..150) */
static void DrawBarValue(uint8_t which /*0=F,1=S*/,
                         uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         uint16_t pct, uint16_t minp, uint16_t maxp)
{
    if (pct < minp) pct = minp;
    if (pct > maxp) pct = maxp;
    if (pct == s_last_bar_val[which]) return;

    /* Innenraum komplett auf Blau (löscht evtl. hängende Pixel) */
    fillRect((int16_t)(x+1), (int16_t)(y+1), (int16_t)(w-2), (int16_t)(h-2), BLUE);

    /* Center-Linie (100%) */
    uint16_t cx = (uint16_t)(x + w/2u);
    drawFastVLine((int16_t)cx, (int16_t)(y+1), (int16_t)(h-2), WHITE);

    /* Rechts (>100%) – nie über den Innenbereich hinaus */
    if (pct > 100u) {
        uint32_t span   = (uint32_t)(maxp - 100u);
        uint32_t rel    = (uint32_t)(pct  - 100u);
        uint16_t len_px = span ? (uint16_t)((rel * (w/2u) + span/2u)/span) : 0u;
        /* Clamp: maximal bis Innenkante rechts (nie den Außenrand berühren) */
        uint16_t len_max = (uint16_t)((w/2u) - 1u);
        if (len_px > len_max) len_px = len_max;
        if (len_px) fillRect((int16_t)cx, (int16_t)(y+1), (int16_t)len_px, (int16_t)(h-2), GREEN);
    }
    /* Links (<100%) – nie vor die Innenkante links */
    else if (pct < 100u) {
        uint32_t span   = (uint32_t)(100u - minp);
        uint32_t rel    = (uint32_t)(100u - pct);
        uint16_t len_px = span ? (uint16_t)((rel * (w/2u) + span/2u)/span) : 0u;
        /* Clamp analog wie rechts */
        uint16_t len_max = (uint16_t)((w/2u) - 1u);
        if (len_px > len_max) len_px = len_max;
        if (len_px) {
            uint16_t start_x = (uint16_t)(cx - len_px);
            if (start_x < (uint16_t)(x+1)) start_x = (uint16_t)(x+1);
            fillRect((int16_t)start_x, (int16_t)(y+1), (int16_t)len_px, (int16_t)(h-2), RED);
        }
    }

    /* Prozentstring: feste 4 Zeichen ("___%") – immer gleiche Position */
    char txt[5];
    (void)snprintf(txt, sizeof(txt), "%3u", (unsigned)pct);
    txt[3] = '%'; txt[4] = '\0';

    /* Zentrierung relativ zur Innenhöhe */
    const uint16_t FONT7_H = 10u;                 // Font_7x10
    uint16_t inner_h = (uint16_t)(h - 2u);
    uint16_t tx = (uint16_t)(x + (w - (4u * CHAR_W))/2u);
    uint16_t ty = (uint16_t)(y + 1u + ((inner_h > FONT7_H) ? (inner_h - FONT7_H)/2u : 0u));

    /* Opaques Schreiben (bg=BLUE) -> kein extra Clear nötig, weniger Flackern */
    ST7735_WriteString(tx, ty, txt, Font_7x10, WHITE, BLUE);

    s_last_bar_val[which] = pct;
}


static void Draw_Value_Aligned(uint8_t idx /*0..5*/, const char* val10)
{
    if (idx > 5) return;

    uint16_t x0 = s_val_x;
    uint16_t y  = s_val_y[idx];

    /* Vergleichen & nur differente Zeichen neu zeichnen */
    const char* old = s_last_val[idx];
    uint8_t oldlen  = s_last_len[idx];

    uint8_t len = (uint8_t)strlen(val10);
    if (len > 10) len = 10;

    uint8_t maxlen = (oldlen > len) ? oldlen : len;
    for (uint8_t i = 0; i < maxlen; ++i){
        char nc = (i < len)    ? val10[i]    : ' ';
        char oc = (i < oldlen) ? old[i]      : ' ';
        if (nc != oc){
            char s[2] = { nc, 0 };
            ST7735_WriteString((uint16_t)(x0 + i*CHAR_W), y, s, Font_13x13, BLACK, WHITE);
        }
    }
    /* Cache aktualisieren */
    memcpy(s_last_val[idx], val10, len);
    s_last_val[idx][len] = 0;
    s_last_len[idx] = len;
}

/* ==== UI-State für Quelle/Timing ==== */
static uint8_t  live_payload[7];
static uint8_t  have_live = 0;

static uint8_t  frame_cache[XHC_FRAME_SIZE];
static uint8_t  have_frame = 0;
static uint32_t frame_t    = 0;

static uint8_t  shown_source = 0; /* 0=nix, 1=LIVE, 2=FRAME */
static uint32_t t_last_draw  = 0;

/* ===================== Public API ===================== */

void RenderScreen_Init(void)
{
    memset(s_last_val, 0, sizeof(s_last_val));
    memset(s_last_len, 0, sizeof(s_last_len));
    memset(s_last_bot, 0, sizeof(s_last_bot));
    memset(s_last_bot_len, 0, sizeof(s_last_bot_len));
    s_static_drawn = 0;
    Draw_Static_Layout_Once();
}

void RenderScreen(void)
{
    Draw_Static_Layout_Once();

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
            if (!have_live) return;
            want = 1;  /* auf Live umschalten */
        } else {
            char v[6][12];  /* 6 Werte je max 11 inkl. 0 */

            /* exakt 10-stellig – Dezimalpunkte in einer Flucht */
            xhc2string_align10(f.pos[0].p_int, f.pos[0].p_frac, v[0]);  /* Xw */
            xhc2string_align10(f.pos[1].p_int, f.pos[1].p_frac, v[1]);  /* Yw */
            xhc2string_align10(f.pos[2].p_int, f.pos[2].p_frac, v[2]);  /* Zw */
            xhc2string_align10(f.pos[3].p_int, f.pos[3].p_frac, v[3]);  /* Xm */
            xhc2string_align10(f.pos[4].p_int, f.pos[4].p_frac, v[4]);  /* Ym */
            xhc2string_align10(f.pos[5].p_int, f.pos[5].p_frac, v[5]);  /* Zm */

            /* ---- Footer: einfache Textwerte im blauen Balken ---- */
            /* FEED: Hundertstel-% → auf ganze % runden und clampen 0..250 */
            uint16_t feed_ovr_raw = rd16_le(frame_cache, OFF_FEED_OVR);
            if (feed_ovr_raw > 25000u) feed_ovr_raw = 25000u;
            uint16_t feed_pct = (uint16_t)((feed_ovr_raw + 50u) / 100u); /* 0..250 */

            /* SPINDLE: ganzzahlig in % → clampen 50..150 */
            uint16_t spin_pct = rd16_le(frame_cache, OFF_SPIND_OVR);
            if (spin_pct < 50u)  spin_pct = 50u;
            if (spin_pct > 150u) spin_pct = 150u;

            /* 100% liegt jeweils in der Mitte */
            DrawBarValue(0, F_BAR_X, BARS_Y, F_BAR_W, BAR_H, feed_pct,  0u, 250u);
            DrawBarValue(1, S_BAR_X, BARS_Y, S_BAR_W, BAR_H, spin_pct, 50u, 150u);

            DrawBarValue(0, F_BAR_X, BARS_Y, F_BAR_W, BAR_H, feed_pct,  0u, 250u);
            DrawBarValue(1, S_BAR_X, BARS_Y, S_BAR_W, BAR_H, spin_pct, 50u, 150u);

            /* --- Rotary POS aktualisieren (mit OFF-Entprellung) --- */
            UpdatePOS_FromRotary();


            for (uint8_t i=0; i<6; ++i) Draw_Value_Aligned(i, v[i]);

            shown_source = 2; t_last_draw = now; return;
        }
    }

    /* ===== LIVE (0x06) ===== */
    if (want == 1){
        char num[16];
        feat06_to_text_align10(live_payload, num, sizeof(num));

        /* Zeige Live ersatzweise in der ersten Zeile (Xw) */
        Draw_Value_Aligned(0, num);

        UpdatePOS_FromRotary();
        /* die übrigen Zeilen werden nicht angerührt */
        shown_source = 1; t_last_draw = now; return;
    }
}
