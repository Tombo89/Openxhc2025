/*
 * xhc_format.h
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_XHC_FORMAT_H_
#define INC_XHC_FORMAT_H_

#pragma once
#include <stdint.h>

void insert_thousand_separators(char *s);
void string2uint(int32_t value, char padd, char *o);
void string2fixed3(int32_t value, char *out);
void string2int(int32_t value, char padd, char *o);

/* HB04/HB03 Positionsformat:
   Vorzeichen steckt im MSB von ifrac (bit15). ipadd/fpadd = Mindestbreite int/fract. */
void xhc2string(uint16_t iint, uint16_t ifrac, char ipadd, char fpadd, char *o);

void int2strprec( int32_t v, char padd, char *o );

void xhc2string_align10(uint16_t iint, uint16_t ifrac, char *out10);


#endif /* INC_XHC_FORMAT_H_ */
