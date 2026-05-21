/*
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
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
#include "hardware/ST7567.h"
#include "main.h"
#include "interfaces/remoteHead.h"

static void ST7567transferCommand(register uint8_t data1);
static void ST7567transferData(uint8_t *rowpos, uint16_t dispsize, uint32_t maxdelay);

static bool isAwake = true;
static bool isInverted = false;

void displayRenderRows(int16_t startRow, int16_t endRow)
{
	if(remoteHeadActive)
	{
		remoteHeadRenderRows(startRow,endRow);
	}
	else
	{
		taskENTER_CRITICAL();

		uint8_t *rowPos = (displayGetScreenBuffer() + startRow * DISPLAY_SIZE_X);

		for(int16_t row = startRow; row < endRow; row++)
		{
			ST7567transferCommand(0xb0 | row); // set Y
			ST7567transferCommand(0x10 | 0); // set X (high MSB)
			ST7567transferCommand(0x04);// 4 pixels from left

			ST7567transferData(rowPos, DISPLAY_SIZE_X, HAL_MAX_DELAY);
			rowPos += DISPLAY_SIZE_X ;
		}

		taskEXIT_CRITICAL();
	}
}

static void ST7567transferData(uint8_t *rowpos, uint16_t dispsize, uint32_t maxdelay)
{
	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_SET);    //Data Mode
	HAL_SPI_Transmit(&hspi2, rowpos, dispsize, maxdelay);
	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static void ST7567transferCommand(uint8_t data1)
{
	if(remoteHeadActive)
	{
		remoteHeadTransferCommand(data1);
	}
	else
	{
		HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_RESET);// command mode// command mode
		HAL_SPI_Transmit(&hspi2, &data1, 1, HAL_MAX_DELAY);
		HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	}
}

void displaySetInverseVideo(bool inverted)
{
	taskENTER_CRITICAL();
	isInverted = inverted;
	if (isInverted)
	{
		ST7567transferCommand(0xA7); // Black background, white pixels
	}
	else
	{
		ST7567transferCommand(0xA4); // White background, black pixels
	}

	ST7567transferCommand(0xAF); // Set Display Enable

	taskEXIT_CRITICAL();
}

void displayBegin(bool inverted)
{
	taskENTER_CRITICAL();

	ST7567transferCommand(0xE2); // System Reset

	ST7567transferCommand(0x2F);// Voltage Follower On
	ST7567transferCommand(0x81);// Set Electronic Volume = 15
	ST7567transferCommand(0x12); // Contrast
	ST7567transferCommand(0xA2); // Set Bias = 1/9
	ST7567transferCommand(0xA1); // SEG Direction
	ST7567transferCommand(0xC0); // COM Direction

	if (inverted)
	{
		ST7567transferCommand(0xA7); // Black background, white pixels
	}
	else
	{
		ST7567transferCommand(0xA4); // White background, black pixels
	}

	ST7567transferCommand(0xAF); // enable

	taskEXIT_CRITICAL();

	displayClearBuf();
	displayRender();
}

void displaySetContrast(uint8_t contrast)
{
	taskENTER_CRITICAL();
	ST7567transferCommand(0x81);              // command to set contrast
	ST7567transferCommand(contrast);          // set contrast
	taskEXIT_CRITICAL();
}


// Note.
// Entering "Sleep" mode makes the display go blank
void displaySetDisplayPowerMode(bool wake)
{
	if (isAwake == wake)
	{
		return;
	}

	taskENTER_CRITICAL();
	isAwake = wake;

	if (wake)
	{
		// enter normal display mode
		if (isInverted)
		{
			ST7567transferCommand(0xA7); // Black background, white pixels
		}
		else
		{
			ST7567transferCommand(0xA4); // White background, black pixels
		}
		ST7567transferCommand(0xAF); // White background, black pixels
	}
	else
	{
		// Enter sleep mode
		ST7567transferCommand(0xAE); // "Set Display OFF" (text from datasheet)
		ST7567transferCommand(0xA5); // "Set All-Pixel-ON" (text from datasheet)
	}

	taskEXIT_CRITICAL();
}
