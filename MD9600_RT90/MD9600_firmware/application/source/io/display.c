/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <FreeRTOS.h>
#include "io/display.h"
#include "hardware/ST7567.h"
#include "functions/settings.h"
#include "interfaces/gpio.h"
#include "main.h"
#if defined(PLATFORM_MD9600)
#include "interfaces/remoteHead.h"
#endif
#include "user_interface/uiGlobals.h"

void displayInit(bool isInverseColour)
{
#if defined(PLATFORM_MD9600)
	if(remoteHeadActive)
	{
		osDelay(10);			//Needs the delay to keep the startup timings correct.
	}
	else
#endif
	{
		// Init pins
		HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, 1);
		HAL_GPIO_WritePin(LCD_Reset_GPIO_Port, LCD_Reset_Pin, 1);
		HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, 1);


		// Reset LCD
		HAL_GPIO_WritePin(LCD_Reset_GPIO_Port, LCD_Reset_Pin, 0);
		osDelay(1);
		HAL_GPIO_WritePin(LCD_Reset_GPIO_Port, LCD_Reset_Pin, 1);
		osDelay(5);
	}

	displayBegin(isInverseColour);
}

void displayEnableBacklight(bool enable, int displayBacklightPercentageOff)
{
	if (enable)
	{
#if defined(PLATFORM_MD9600)
		if(remoteHeadActive)
		{
			remoteHeadDim(nonVolatileSettings.displayBacklightPercentage[DAYTIME_CURRENT]);
		}
		else
#endif
		{
			gpioSetDisplayBacklightIntensityPercentage(nonVolatileSettings.displayBacklightPercentage[DAYTIME_CURRENT]);
		}
	}
	else
	{
#if defined(PLATFORM_MD9600)
		if(remoteHeadActive)
		{
			remoteHeadDim(((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_NONE) ? 0 : displayBacklightPercentageOff));
		}
		else
#endif
		{
			gpioSetDisplayBacklightIntensityPercentage(((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_NONE) ? 0 : displayBacklightPercentageOff));
		}
	}

	//HAL_GPIO_WritePin(Display_Light_GPIO_Port, Display_Light_Pin, enable);//Enable the LCD backlight and mic key backlight leds
}

bool displayIsBacklightLit(void)
{
	return (gpioGetDisplayBacklightIntensityPercentage() != nonVolatileSettings.displayBacklightPercentageOff);
}


