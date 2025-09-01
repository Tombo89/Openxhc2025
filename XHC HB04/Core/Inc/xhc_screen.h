/*
 * xhc_screen.h
 *
 *  Created on: Sep 1, 2025
 *      Author: Thomas Weckmann
 */

#ifndef INC_XHC_SCREEN_H_
#define INC_XHC_SCREEN_H_

#pragma once

/* Einmal aufrufen nach Display-Init */
void RenderScreen_Init(void);

/* In der main-While-Schleife aufrufen */
void RenderScreen(void);


#endif /* INC_XHC_SCREEN_H_ */
