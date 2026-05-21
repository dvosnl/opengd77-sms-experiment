/*
 * Copyright (C) 2020-2025 Roger Clark, VK3KYY / G4KYF
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
#include "stm32f4xx_hal.h"
#include "main.h"
#include "interfaces/gpio.h"
#include <stdint.h>

static uint8_t currentDisplayPercentage = 255U;

void gpioInitButtons(void)
{
}

void gpioInitCommon(void)
{
}

void gpioInitDisplay(void)
{

}

void gpioSetDisplayBacklightIntensityPercentage(uint8_t intensityPercentage)
{
	int PWMValue;								//PWM value is 0-3000 which is equal to 0-100%

	switch(intensityPercentage)
	{
		case 0U:
			PWMValue = 0;								//force LEDs full off at 0%
			break;
		case 100U:
			PWMValue = 3000;							//force LEDs fully on at 100%
			break;
		default:
#if defined(MD9600_VERSION_1)
			PWMValue = intensityPercentage * 20;				//V1 radios have different Backlight hardware and so need a larger range of adjustment.
#else
			PWMValue = 480 + intensityPercentage;       // 480/3000 is 16% PWM. This is the level where LEDS are just off.  480 + 100 = 580 which is 19.3% THis is the level where LEDs are fully on

#endif
			break;
	}

	if (currentDisplayPercentage != intensityPercentage)
	{
		currentDisplayPercentage = intensityPercentage;

		TIM_OC_InitTypeDef sConfigOC;
	    sConfigOC.OCMode = TIM_OCMODE_PWM1;
	    sConfigOC.Pulse = PWMValue;
	    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
	    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);
	    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	}
}

uint8_t gpioGetDisplayBacklightIntensityPercentage(void)
{
	return currentDisplayPercentage;
}

void gpioInitFlash(void)
{

}

void gpioInitKeyboard(void)
{

}

void gpioInitLEDs(void)
{

}

void gpioInitRotarySwitch(void)
{

}

void gpioInitC6000Interface(void)
{
}
