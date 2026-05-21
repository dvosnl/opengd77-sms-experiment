/*
 * Copyright (C) 2021-2025 Roger Clark, VK3KYY / G4KYF
 *                         Colin Durbridge, G4EML
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
#include "main.h"
#include "interfaces/remoteHead.h"
#include "user_interface/uiGlobals.h"
#include "interfaces/gps.h"

bool remoteHeadActive = false;
uint16_t remoteHeadKey = 0;
uint8_t remoteHeadMicRow = 5;
uint8_t remoteHeadMicCol = 4;
uint8_t remoteHeadPTT = 0;
int8_t remoteHeadRotary = 0;

static uint8_t remoteDMABuf[REMOTE_DMA_BUFFER_SIZE]; // double buffer (two halves) for UART DMA receive
static uint8_t remoteTxBuff[2048];						//DMA buffer for Serial transmit
static int txBuffPoint;
static uint8_t currentDisplayPercentage = 255;
static bool remoteSendBusy = false;

static bool remoteDetect(void)
{
	return (HAL_GPIO_ReadPin(KEYPAD_COL2_K4_GPIO_Port, KEYPAD_COL2_K4_Pin) == GPIO_PIN_RESET);
}

static void remoteInputStart(void)
{
	HAL_UART_Receive_DMA(&huart3, remoteDMABuf, REMOTE_DMA_BUFFER_SIZE);   //start the DMA receiver
}

static void processRemoteInput(uint8_t command, uint8_t byte1, uint8_t byte2)
{
	switch (command)
	{
		case 'K':

			remoteHeadPTT = (((byte1 & 0x80) == 0x80) ? 1 : 0); //PTT is pressed

			if ((byte1 & 0x40) == 0x40)		//CW rotary
			{
				remoteHeadRotary = 1;
			}

			if ((byte1 & 0x20) == 0x20)		//CCW rotary
			{
				remoteHeadRotary = -1;
			}

			remoteHeadKey = ((byte1 & 0x1F) << 8) | byte2;
			break;

		case 'M':
			remoteHeadMicRow = byte1;
			remoteHeadMicCol = byte2;
			break;

		case 'G':						//GPS character
			gpsProcessChar(byte1);
			if (byte2 > 0)
			{
				gpsProcessChar(byte2);
			}
			break;
	}

}

static void remoteStartTx(void)
{
	HAL_UART_Transmit_DMA(&huart3, remoteTxBuff, txBuffPoint);
}

//HAl callback is already defined in the GPS module. if the callback is for the remote it will be redirected here.
void remoteHead_UART_RxHalfCpltCallback(void)
{
	processRemoteInput(remoteDMABuf[0], remoteDMABuf[1], remoteDMABuf[2]);
}

void remoteHead_UART_RxCpltCallback(void)
{
	processRemoteInput(remoteDMABuf[3], remoteDMABuf[4], remoteDMABuf[5]);
}

//This Callback happens when the Serial Transmission is finished.
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	remoteSendBusy = false;
}

void remoteHeadInit(void)
{
	if (remoteDetect()) // if Remote head is detected then change ROTARY_SW_A and ROTARY_SW_B control pins to be USART 3 for data transfer.
	{
		remoteHeadActive = true;
		HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);			//disable Rotary Interrupts
		rotaryData.Count = 0;							//Clear any pre-existing spurious rotary interrupts.
		rotaryData.Direction = 0;
		MX_USART3_UART_Init();							//Enable Remote Head Comms
		remoteInputStart();
		vTaskDelay(100);									//allow time for the front panel keys to be read. (needed by Restore Settings)
	}
}

//request the remote Head to re-send its key inputs.
void remoteHeadRestartIP(void)
{
	while (remoteSendBusy)			//if already busy wait for it to complete
	{
		vTaskDelay(1);
	}

	remoteSendBusy = true;
	txBuffPoint = 0;
	remoteTxBuff[txBuffPoint++] = 'R';
	remoteTxBuff[txBuffPoint++] = 0;
	remoteStartTx();
}

//direct send of command to remote head. Not using DMA. (this is needed during display init).
void remoteHeadTransferCommand(uint8_t data1)
{
	uint8_t com[2];

	com[0] = 'C';
	com[1] = data1;
	HAL_UART_Transmit(&huart3, com, 2, HAL_MAX_DELAY);
}

static void remoteAddCommand(uint8_t com)
{
	remoteTxBuff[txBuffPoint++] = 'C';
	remoteTxBuff[txBuffPoint++] = com;
}

static void remoteAddData(uint8_t *dat, uint8_t len)
{
	remoteTxBuff[txBuffPoint++] = 'D';
	remoteTxBuff[txBuffPoint++] = len;
	memcpy(&remoteTxBuff[txBuffPoint], dat, len);
	txBuffPoint = txBuffPoint + len;
}

void remoteHeadRenderRows(int16_t startRow, int16_t endRow)
{
	while (remoteSendBusy)			//if already busy wait for it to complete
	{
		vTaskDelay(1);
	}

	remoteSendBusy = true;

	taskENTER_CRITICAL();
	txBuffPoint = 0;

	uint8_t *rowPos = (displayGetScreenBuffer() + startRow * DISPLAY_SIZE_X);

	for (int16_t row = startRow; row < endRow; row++)
	{
		remoteAddCommand(0xb0 | row); // set Y
		remoteAddCommand(0x10 | 0); // set X (high MSB)
		remoteAddCommand(0x04); // 4 pixels from left

		remoteAddData(rowPos, DISPLAY_SIZE_X);
		rowPos += DISPLAY_SIZE_X;
	}

	remoteStartTx();
	taskEXIT_CRITICAL();
}

void remoteHeadGpsPower(bool on)
{
	while (remoteSendBusy)			//if already busy wait for it to complete
	{
		vTaskDelay(1);
	}

	remoteSendBusy = true;
	txBuffPoint = 0;
	remoteTxBuff[txBuffPoint++] = 'P';
	remoteTxBuff[txBuffPoint++] = (on ? 1 : 0);

	remoteStartTx();
}

void remoteHeadGpsDataInputStartStop(bool on)
{
	while (remoteSendBusy)			//if already busy wait for it to complete
	{
		vTaskDelay(1);
	}

	remoteSendBusy = true;
	txBuffPoint = 0;
	remoteTxBuff[txBuffPoint++] = 'G';
	remoteTxBuff[txBuffPoint++] = (on ? 1 : 0);

	remoteStartTx();
}

void remoteHeadDim(uint8_t bright)
{
	int PWMValue;								//PWM value is 0-255 which is equal to 0-100%

	switch (bright)
	{
		case 0:
			PWMValue = 0;								//force LEDs full off at 0%
			break;

		case 100:
			PWMValue = 255;							//force LEDs fully on at 100%
			break;

		default:
			PWMValue = (bright * 255) / 100;       //  convert 0-100 to 0-255
			break;
	}

	if (currentDisplayPercentage != bright)
	{
		currentDisplayPercentage = bright;

		while (remoteSendBusy)			//if already busy wait for it to complete
		{
			vTaskDelay(1);
		}

		remoteSendBusy = true;
		txBuffPoint = 0;
		remoteTxBuff[txBuffPoint++] = 'B';
		remoteTxBuff[txBuffPoint++] = PWMValue;

		remoteStartTx();
	}
}

void remoteHeadPowerDown(void)
{
	while (remoteSendBusy)			//if already busy wait for it to complete
	{
		vTaskDelay(1);
	}

	remoteSendBusy = true;
	txBuffPoint = 0;
	remoteTxBuff[txBuffPoint++] = 'O';
	remoteTxBuff[txBuffPoint++] = 0;

	remoteStartTx();
	vTaskDelay(10);
}
