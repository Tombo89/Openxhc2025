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
    char temp[24];   // Zwischenspeicher
    char *s = temp;  char c = 0; char is_negative = 0;

    if (ifrac & 0x8000u){ is_negative = 1; ifrac &= ~0x8000u; }

    /* Nachkommastellen */
    do { *s++ = (ifrac % 10) + '0'; ifrac /= 10; c++; } while (ifrac);
    while (c < fpadd) { *s++ = '0'; c++; }
    *s++ = '.';

    /* Ganzzahlteil */
    c = 0;
    do { *s++ = (iint % 10) + '0'; iint /= 10; c++; } while (iint);
    *s = 0;

    /* Umkehren */
    char *start=temp, *end=s-1;
    while (start < end){ char t=*start; *start++=*end; *end--=t; }

    /* Längen & Padding */
    uint8_t temp_len = 0; while (temp[temp_len]) temp_len++;
    uint8_t total_len = ipadd + fpadd + 3;  // +3 für "- ", "."
    uint8_t used_len  = temp_len + 2;       // +2 für "- " oder "  "
    uint8_t padding   = (total_len > used_len) ? (total_len - used_len) : 0;

    while (padding--) *o++ = ' ';
    *o++ = is_negative ? '-' : ' ';
    *o++ = ' ';
    for (uint8_t i=0; temp[i]; i++) *o++ = temp[i];
    *o = 0;
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

