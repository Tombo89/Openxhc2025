/*
 * xhc_display.c
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#include "xhc_display.h"
#include "ST7735.h"
#include "fonts.h"
#include <string.h>

#ifndef WHITE
#define WHITE 0xFFFF
#endif
#ifndef BLACK
#define BLACK 0x0000
#endif

#define LINE_H   12
#define CHAR_W    7
#define MAX_COLS  40

/* pro Zeile (0..3) Cache, um nur geänderte Zeichen zu zeichnen */
static char    s_last[4][MAX_COLS];
static uint8_t s_len[4];

static void put_char(uint16_t x, uint16_t y, char c)
{
    char s[2] = { c, 0 };
    ST7735_WriteString(x, y, s, Font_7x10, WHITE, BLACK);
}

static void draw_line_diff(uint8_t row, const char* text)
{
    if (row > 3) row = 3;
    size_t len = strlen(text);
    if (len > MAX_COLS) len = MAX_COLS;
    size_t oldlen = s_len[row];
    size_t maxlen = (len > oldlen) ? len : oldlen;

    uint16_t y = (uint16_t)(row * LINE_H);
    for (size_t i=0; i<maxlen; ++i){
        char nc = (i < len) ? text[i] : ' ';
        char oc = (i < oldlen) ? s_last[row][i] : ' ';
        if (nc != oc){
            put_char((uint16_t)(i*CHAR_W), y, nc);
        }
    }
    memcpy(s_last[row], text, len);
    s_len[row] = (uint8_t)len;
}

void XHC_Display_Init(void)
{
    memset(s_last, 0, sizeof(s_last));
    memset(s_len,  0, sizeof(s_len));
    // Screen muss vorher von dir initialisiert worden sein (ST7735_Init/Fill etc.)
}

void XHC_Display_SetHeader(const char *text) { draw_line_diff(0, text); }
void XHC_Display_SetLine(uint8_t line_idx, const char *text)
{
    if (line_idx < 1 || line_idx > 3) return;
    draw_line_diff(line_idx, text);
}

