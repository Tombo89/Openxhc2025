/*
 * xhc_format.c
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#include "xhc_format.h"

/* --- interner Helper --- */
static void strreverse(char *b, char *e)
{
    char a;
    while(e>b){ a=*e; *e--=*b; *b++=a; }
}

/* === deine Alt-Helfer 1:1 (leicht gesäubert) === */

void insert_thousand_separators(char *s)
{
    int len = 0; while(s[len]) len++;

    int digit_count = 0;
    for (int i = 0; i < len; i++)
        if (s[i] >= '0' && s[i] <= '9') digit_count++;

    if (digit_count <= 3) return;

    char buffer[32];
    int buf_idx = 0;

    int first_group_len = digit_count % 3;
    if (first_group_len == 0) first_group_len = 3;

    int digits_seen = 0;

    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            buffer[buf_idx++] = c;
            digits_seen++;
            if ((digits_seen == first_group_len || ((digits_seen - first_group_len) % 3 == 0))
                 && digits_seen != digit_count) {
                buffer[buf_idx++] = '.';
            }
        } else {
            buffer[buf_idx++] = c;
        }
    }
    buffer[buf_idx] = 0;

    for (int i = 0; i <= buf_idx; i++) s[i] = buffer[i];
}

void string2uint( int32_t value, char padd, char *o )
{
    char *s = o; char c = 0;
    do { *o++ = (value%10)+'0'; value/=10; ++c; } while( value );
    while( c < padd ){ *o++ = ' '; ++c; }
    *o = 0; strreverse( s, o-1 ); insert_thousand_separators(s);
}

void string2fixed3(int32_t value, char *out)
{
    if (value < 1000) {
        out[0]='0'; out[1]='.'; out[2]=(value/100)+'0';
        out[3]=((value/10)%10)+'0'; out[4]=(value%10)+'0'; out[5]=0;
    } else {
        int32_t int_part = value / 1000;
        int32_t frac_part = value % 1000;
        char *p = out;
        do { *p++ = (int_part % 10) + '0'; int_part /= 10; } while (int_part);
        *p = 0; strreverse(out, p - 1);
        *p++ = '.';
        *p++ = (frac_part / 100) + '0';
        *p++ = ((frac_part / 10) % 10) + '0';
        *p++ = (frac_part % 10) + '0';
        *p = 0;
    }
}

void string2int( int32_t value, char padd, char *o )
{
    char *s = o; char c = 0; char sign = ' ';
    if( value < 0 ){ sign='-'; value = -value; }
    do { *o++ = (value%10)+'0'; value/=10; ++c; } while( value );
    while( c < padd ){ *o++ = ' '; ++c; }
    *o++ = sign; *o = 0; strreverse( s, o-1 );
}

void xhc2string(uint16_t iint, uint16_t ifrac, char ipadd, char fpadd, char *o)
{
    /* Vorzeichen steckt im MSB von ifrac */
    uint8_t neg = 0;
    if (ifrac & 0x8000u) { neg = 1; ifrac &= 0x7FFFu; }

    /* Integerteil als String (ohne Vorzeichen) */
    char buf_int[12];
    int  int_len = 0;
    uint32_t ii = iint;
    do {
        buf_int[int_len++] = (char)('0' + (ii % 10));
        ii /= 10;
    } while (ii && int_len < (int)sizeof(buf_int));
    // reverse
    for (int a=0, b=int_len-1; a<b; ++a, --b) { char t=buf_int[a]; buf_int[a]=buf_int[b]; buf_int[b]=t; }
    buf_int[int_len] = '\0';

    /* Nachkommastellen exakt fpadd Ziffern */
    if (fpadd < 0) fpadd = 0;
    if (fpadd > 7) fpadd = 7;  // Sicherheitskappe
    char buf_frac[8];
    for (int i=fpadd-1; i>=0; --i) { buf_frac[i] = (char)('0' + (ifrac % 10)); ifrac /= 10; }
    buf_frac[fpadd] = '\0';

    /* Padding vor dem Vorzeichen/Zahl, um min. ipadd Integerbreite zu erreichen */
    int pad_left = 0;
    if (ipadd > int_len) pad_left = ipadd - int_len;

    char *out = o;
    while (pad_left-- > 0) *out++ = ' ';
    if (neg) *out++ = '-';               // <- direkt vor die Zahl, keine Lücke danach
    for (int i=0; i<int_len; ++i) *out++ = buf_int[i];
    *out++ = '.';
    for (int i=0; i<fpadd;  ++i) *out++ = buf_frac[i];
    *out = '\0';
}

void int2strprec( int32_t v, char padd, char *o )
{
    char *s = o; char c = 0; char sign = ' ';
    if( v < 0 ){ sign='-'; v = -v; }
    do {
      *o++ = (v%10)+'0'; ++c;
      if( c == 2 ){ *o++ = '.'; }
      v/=10;
    } while( v );
    while( c < padd ){ *o++ = ' '; }
    *o++ = sign; *o = 0; strreverse( s, o-1 );
}

void xhc2string_align10(uint16_t iint, uint16_t ifrac, char *out10)
{
    /* Vorzeichen steckt im MSB von ifrac */
    uint8_t neg = 0;
    if (ifrac & 0x8000u) { neg = 1; ifrac &= 0x7FFFu; }

    /* Integerteil (ohne Vorzeichen) in buf_int */
    char buf_int[6];  // max 4 Ziffern + '\0'
    int  int_len = 0;
    uint32_t ii = iint;
    do {
        buf_int[int_len++] = (char)('0' + (ii % 10));
        ii /= 10;
    } while (ii && int_len < 5);
    // reverse
    for (int a=0, b=int_len-1; a<b; ++a, --b) { char t=buf_int[a]; buf_int[a]=buf_int[b]; buf_int[b]=t; }
    buf_int[int_len] = '\0';

    // Safety: wenn mehr als 4 Stellen nötig wären → "####"
    if (int_len > 4) {
        int_len = 4;
        buf_int[0] = '#'; buf_int[1] = '#'; buf_int[2] = '#'; buf_int[3] = '#'; buf_int[4] = '\0';
    }

    /* Pre-Field vor dem Punkt: 5 Spalten (inkl. evtl. Minus) */
    char pre[5];
    for (int i=0;i<5;i++) pre[i] = ' ';

    // Ziffern rechtsbündig in pre[ ] einfüllen
    int pos = 4;
    for (int d = int_len-1; d >= 0; --d) {
        pre[pos--] = buf_int[d];
    }
    // Minus direkt vor die erste Ziffer (also genau an 'pos')
    if (neg && pos >= 0) {
        pre[pos] = '-';
    }

    /* Nachkomma: genau 4 Ziffern, führend mit Nullen */
    char frac[4];
    uint32_t fr = ifrac;
    for (int i=3; i>=0; --i) { frac[i] = (char)('0' + (fr % 10)); fr /= 10; }

    /* Ausgabe: pre[0..4] '.' frac[0..3]  → Länge = 10 */
    char *o = out10;
    for (int i=0;i<5;i++) *o++ = pre[i];
    *o++ = '.';
    for (int i=0;i<4;i++) *o++ = frac[i];
    *o = '\0';
}
