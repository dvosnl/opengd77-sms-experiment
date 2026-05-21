/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2020 Alex, DL4LEX
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *                         Colin Durbridge, G4EML
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
#include "io/keyboard.h"
#include "interfaces/pit.h"
#include "functions/settings.h"
#include "interfaces/gpio.h"
#include "interfaces/adc.h"
#include "io/buttons.h"
#include "interfaces/remoteHead.h"

// Front Panel Buttons
typedef struct
{
	GPIO_TypeDef  *GPIOPort;
	uint16_t       GPIOPin;
	uint16_t       Key;
} FrontKeySetting_t;

typedef uint8_t MIC_BUTTON_t;
static char oldKeyboardCode;
static uint32_t keyDebounceScancode;
static int keyDebounceCounter;
static uint8_t keyState;
static char keypadAlphaKey;
static int keypadAlphaIndex;
volatile bool keypadAlphaEnable;
volatile bool keypadLocked = false;

#define MIC_NUMBER_OF_COLS    4
#define MIC_NUMBER_OF_ROWS    5

static const uint8_t MIC_BUTTONS_MAP[] =
{
		'1', '2', '3', KEY_A,
		'4', '5', '6', KEY_B,
		'7', '8', '9', KEY_C,
		KEY_STAR, '0', KEY_HASH, KEY_D,
		'X', KEY_GREEN, KEY_UP, KEY_DOWN
};

volatile rotaryData_t rotaryData =
{
		.Count = 0,
		.lastA = (GPIO_PinState)(GPIO_PIN_SET + 1), // To be sure it will be different on the first run (Hackish ? Yeah !)
		.Direction = 0
};

enum KEY_STATE
{
	KEY_IDLE = 0,
	KEY_DEBOUNCE,
	KEY_PRESS,
	KEY_WAITLONG,
	KEY_REPEAT,
	KEY_WAIT_RELEASED
};

/* Not currently used
static const MIC_BUTTON_t MIC_BUTTONS[] =
{
		MIC_BUTTON_1,       MIC_BUTTON_2,  MIC_BUTTON_3,    MIC_BUTTON_A,
		MIC_BUTTON_4,       MIC_BUTTON_5,  MIC_BUTTON_6,    MIC_BUTTON_B,
		MIC_BUTTON_7,       MIC_BUTTON_8,  MIC_BUTTON_9,    MIC_BUTTON_C,
		MIC_BUTTON_STAR,    MIC_BUTTON_0,  MIC_BUTTON_HASH, MIC_BUTTON_D,
		MIC_BUTTON_UNKNOWN, MIC_BUTTON_AB, MIC_BUTTON_UP,   MIC_BUTTON_DOWN
};
 */
static const char keypadAlphaMap[11][31] = {
		"0 ",
		"1.!,@-:?()~/[]#<>=*+$%'`&|_^{}",
		"abc2ABC",
		"def3DEF",
		"ghi4GHI",
		"jkl5JKL",
		"mno6MNO",
		"pqrs7PQRS",
		"tuv8TUV",
		"wxyz9WXYZ",
		"*"
};

#define FRONT_KEYS_PER_ROW  3
static const struct {
	GPIO_TypeDef       *GPIOCtrlPort;
	uint16_t            GPIOCtrlPin;
	FrontKeySetting_t   settings[FRONT_KEYS_PER_ROW];
} FrontKeysSettings[] =
{
		{
				KEYPAD_ROW0_K0_GPIO_Port,  KEYPAD_ROW0_K0_Pin,
				{
						{ KEYPAD_COL0_K6_GPIO_Port,  KEYPAD_COL0_K6_Pin,  FRONT_KEY_POWER },
						{ KEYPAD_COL1_K5_GPIO_Port,  KEYPAD_COL1_K5_Pin,  FRONT_KEY_ALARM },
						{ KEYPAD_COL2_K4_GPIO_Port,  KEYPAD_COL2_K4_Pin,  FRONT_KEY_P1    }
				}
		},
		{
				KEYPAD_ROW1_K1_GPIO_Port,  KEYPAD_ROW1_K1_Pin,
				{
						{ KEYPAD_COL0_K6_GPIO_Port,  KEYPAD_COL0_K6_Pin,  FRONT_KEY_P2    },
						{ KEYPAD_COL1_K5_GPIO_Port,  KEYPAD_COL1_K5_Pin,  FRONT_KEY_P3    },
						{ KEYPAD_COL2_K4_GPIO_Port,  KEYPAD_COL2_K4_Pin,  FRONT_KEY_P4    }
				}
		},
		{
				KEYPAD_ROW2_K2_GPIO_Port,  KEYPAD_ROW2_K2_Pin,
				{
						{ KEYPAD_COL0_K6_GPIO_Port,  KEYPAD_COL0_K6_Pin,  FRONT_KEY_BACK  },
						{ KEYPAD_COL1_K5_GPIO_Port,  KEYPAD_COL1_K5_Pin,  FRONT_KEY_DOWN  },
						{ KEYPAD_COL2_K4_GPIO_Port,  KEYPAD_COL2_K4_Pin,  FRONT_KEY_ENT   }
				}
		},
		{
				KEYPAD_ROW3_K3_GPIO_Port,  KEYPAD_ROW3_K3_Pin,
				{
						{ KEYPAD_COL0_K6_GPIO_Port,  KEYPAD_COL0_K6_Pin,  FRONT_KEY_UP    },
						{ NULL,                      0,                   0               },
						{ NULL,                      0,                   0               }
				}
		}
};

void buttonsFrontPanelInit(void)
{
	HAL_GPIO_WritePin(KEYPAD_ROW0_K0_GPIO_Port, KEYPAD_ROW0_K0_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(KEYPAD_ROW1_K1_GPIO_Port, KEYPAD_ROW1_K1_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(KEYPAD_ROW2_K2_GPIO_Port, KEYPAD_ROW2_K2_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(KEYPAD_ROW3_K3_GPIO_Port, KEYPAD_ROW3_K3_Pin, GPIO_PIN_SET);
}

uint16_t buttonsFrontPanelRead(void)
{
	uint16_t keys;

	if(remoteHeadActive)
	{
		keys = remoteHeadKey;
	}
	else
	{
		keys = FRONT_KEY_NONE;

		for (size_t row = 0; row < (sizeof(FrontKeysSettings) / sizeof(FrontKeysSettings[0])); row++)
		{
			HAL_GPIO_WritePin(FrontKeysSettings[row].GPIOCtrlPort, FrontKeysSettings[row].GPIOCtrlPin, GPIO_PIN_RESET);

			for (volatile int i = 0; i < 100; i++)
				; // small delay to allow voltages to settle. The delay value of 100 is arbitrary.

			for (uint8_t col = 0; col < FRONT_KEYS_PER_ROW; col++)
			{
				if (FrontKeysSettings[row].settings[col].GPIOPort == NULL)
				{
					break;
				}

				if (HAL_GPIO_ReadPin(FrontKeysSettings[row].settings[col].GPIOPort, FrontKeysSettings[row].settings[col].GPIOPin) == GPIO_PIN_RESET)
				{
					keys |= FrontKeysSettings[row].settings[col].Key;
				}
			}

			HAL_GPIO_WritePin(FrontKeysSettings[row].GPIOCtrlPort, FrontKeysSettings[row].GPIOCtrlPin, GPIO_PIN_SET);
		}

	}

	return keys;
}

void keyboardInit(void)
{
	gpioInitKeyboard();

	oldKeyboardCode = 0;
	keyDebounceScancode = 0;
	keyDebounceCounter = 0;
	keypadAlphaEnable = false;
	keypadAlphaIndex = 0;
	keypadAlphaKey = 0;
	keyState = KEY_IDLE;
	keypadLocked = false;
}

void keyboardReset(void)
{
	oldKeyboardCode = 0;
	keypadAlphaEnable = false;
	keypadAlphaIndex = 0;
	keypadAlphaKey = 0;
	keyState = KEY_WAIT_RELEASED;
}

bool keyboardKeyIsDTMFKey(char key)
{
	switch (key)
	{
		case KEY_0 ... KEY_9:
		case KEY_STAR:
		case KEY_HASH:
		case KEY_A:
		case KEY_B:
		case KEY_C:
		case KEY_D:
			return true;
	}
	return false;
}

static bool buttonsMicGetRowCol(uint32_t *row, uint32_t *col)
{
	if((remoteHeadActive) && ((remoteHeadMicRow < 5) || (remoteHeadMicCol < 4))) //allow use of either remote mic or local mic buttons
	{
		*row = (MIC_NUMBER_OF_ROWS - 1) - remoteHeadMicRow;     // Rows are upside down, revert it
		*col = remoteHeadMicCol;
	}
	else
	{
		*row = ((MIC_NUMBER_OF_ROWS - 1) - ((int)(adcGetMicRow() / 800))); // Rows are upside down, revert it
		*col = (int)(adcGetMicCol() / 1000);
	}

	return true;
}


/*
static bool buttonsMicGetRowCol(uint32_t *row, uint32_t *col)
{
	// Get ADC values for Col and Row
	if (HAL_ADC_Start(&hadc1) == HAL_OK)
	{
		if (HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY) == HAL_OK)
		{
			uint32_t rowVal, colVal;

			rowVal = HAL_ADC_GetValue(&hadc1);

			if (HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY) == HAL_OK)
			{
				colVal = HAL_ADC_GetValue(&hadc1);

				if (HAL_ADC_Stop(&hadc1) != HAL_OK)
				{
					// Ignore ?
				}

 *row = ((MIC_NUMBER_OF_ROWS - 1) - ((int)(rowVal / 800))); // Rows are upside down, revert it
 *col = (int)(colVal / 1000);

				return true;
			}
		}
	}

	return false;
}*/

uint8_t micButtonsRead(void)
{
	uint32_t rowOffset;
	uint32_t colOffset;

	if (buttonsMicGetRowCol(&rowOffset, &colOffset))
	{
		if ((rowOffset < MIC_NUMBER_OF_ROWS) && (colOffset < MIC_NUMBER_OF_COLS)) // A button is down
		{
			return MIC_BUTTONS_MAP[(((MIC_NUMBER_OF_COLS * rowOffset) + (colOffset + 1)) - 1)];
		}
	}

	return 0x00;
}

void rotaryEncoderISR(void)
{
	if(!remoteHeadActive)
	{
		GPIO_PinState pinA = HAL_GPIO_ReadPin(ROTARY_SW_A_GPIO_Port, ROTARY_SW_A_Pin);
		GPIO_PinState pinB = HAL_GPIO_ReadPin(ROTARY_SW_B_GPIO_Port, ROTARY_SW_B_Pin);

		if (pinA != rotaryData.lastA)
		{
			rotaryData.lastA = pinA;		// Inc/Dec according to rotation
			rotaryData.Direction = ((pinA != pinB) ? 1 : -1);
			rotaryData.Count += rotaryData.Direction;
		}
	}
}

void keyboardCheckKeyEvent(keyboardCode_t *keys, int *event, uint16_t frontPanelButtons)
{
	uint32_t scancode = 0;
	char keycode;
	bool validKey;
	int newAlphaKey;
	uint32_t tmp_timer_keypad;
	uint32_t keypadTimerLong = nonVolatileSettings.keypadTimerLong * 100;
	uint32_t keypadTimerRepeat = nonVolatileSettings.keypadTimerRepeat * 100;

	*event = EVENT_KEY_NONE;
	keys->event = 0;
	keys->key = 0;

	if((remoteHeadActive) && (remoteHeadRotary != 0))
	{
		rotaryData.Direction = remoteHeadRotary;
		remoteHeadRotary = 0;
	}

	if (rotaryData.Direction != 0)
	{
		keys->key = (rotaryData.Direction == 1) ? KEY_ROTARY_INCREMENT : KEY_ROTARY_DECREMENT;
		keys->event = KEY_MOD_UP | KEY_MOD_PRESS; // Hack send both Up and Down events because the menus use KEY_MOD_PRESS but the VFO uses KEY_MOD_UP
		*event = EVENT_KEY_CHANGE;
		rotaryData.Direction = 0;
		return;
	}
	else
	{
		keycode = micButtonsRead();

		// Convert front panel buttons to GD77 key codes for Enter -> Green, Esc/Back -> Red, Up and Down
		if (frontPanelButtons != 0)
		{
			if  ((frontPanelButtons & FRONT_KEY_POWER) == FRONT_KEY_POWER)
			{
				keycode |= KEY_POWER;
			}

			if  ((frontPanelButtons & FRONT_KEY_UP) == FRONT_KEY_UP)
			{
				keycode |= KEY_FRONT_UP;
			}

			if  ((frontPanelButtons & FRONT_KEY_DOWN) == FRONT_KEY_DOWN)
			{
				keycode |= KEY_FRONT_DOWN;
			}

			if  ((frontPanelButtons & FRONT_KEY_ENT) == FRONT_KEY_ENT)
			{
				keycode |= KEY_GREEN;
			}

			if  ((frontPanelButtons & FRONT_KEY_BACK) == FRONT_KEY_BACK)
			{
				keycode |= KEY_RED;
			}

			/*
			 * ALARM, P1 and P2 are handled as buttons
			 *
			 */

			if  ((frontPanelButtons & FRONT_KEY_P4) == FRONT_KEY_P4)
			{
				keycode |= KEY_STAR;
			}
		}

		scancode = keycode;
	}

	validKey = true;

	if (keyState > KEY_DEBOUNCE && !validKey)
	{
		keyState = KEY_WAIT_RELEASED;
	}

	switch (keyState)
	{
		case KEY_IDLE:
			if (keycode != 0)
			{
				keyState = KEY_DEBOUNCE;
				keyDebounceCounter = 0;
				keyDebounceScancode = scancode;
				oldKeyboardCode = 0;
			}
			taskENTER_CRITICAL();
			tmp_timer_keypad = timer_keypad_timeout;
			taskEXIT_CRITICAL();

			if (tmp_timer_keypad == 0 && keypadAlphaKey != 0)
			{
				keys->key = keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex];
				keys->event = KEY_MOD_PRESS;
				*event = EVENT_KEY_CHANGE;
				keypadAlphaKey = 0;
			}
			break;
		case KEY_DEBOUNCE:
			keyDebounceCounter++;
			if (keyDebounceCounter > KEY_DEBOUNCE_COUNTER)
			{
				if (keyDebounceScancode == scancode)
				{
					oldKeyboardCode = keycode;
					keyState = KEY_PRESS;
				}
				else
				{
					keyState = KEY_WAIT_RELEASED;
				}
			}
			break;
		case KEY_PRESS:
			keys->key = keycode;
			keys->event = KEY_MOD_DOWN | KEY_MOD_PRESS;
			*event = EVENT_KEY_CHANGE;

			taskENTER_CRITICAL();
			timer_keypad = keypadTimerLong;
			timer_keypad_timeout = 1000;
			taskEXIT_CRITICAL();
			keyState = KEY_WAITLONG;

			if (keypadAlphaEnable)
			{
				newAlphaKey = 0;
				if ((keycode >= '0') && (keycode <= '9'))
				{
					newAlphaKey = (keycode - '0') + 1;
				}
				else if (keycode == KEY_STAR)
				{
					newAlphaKey = 11;
				}

				if (keypadAlphaKey == 0)
				{
					if (newAlphaKey != 0)
					{
						keypadAlphaKey = newAlphaKey;
						keypadAlphaIndex = 0;
					}
				}
				else
				{
					if (newAlphaKey == keypadAlphaKey)
					{
						keypadAlphaIndex++;
						if (keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex] == 0)
						{
							keypadAlphaIndex = 0;
						}
					}
				}

				if (keypadAlphaKey != 0)
				{
					if (newAlphaKey == keypadAlphaKey)
					{
						keys->key =	keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex];
						keys->event = KEY_MOD_PREVIEW;
					}
					else
					{
						keys->key = keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex];
						keys->event = KEY_MOD_PRESS;
						*event = EVENT_KEY_CHANGE;
						keypadAlphaKey = newAlphaKey;
						keypadAlphaIndex = -1;
						keyState = KEY_PRESS;
					}
				}
			}
			break;
		case KEY_WAITLONG:
			if (keycode == 0)
			{
				keys->key = oldKeyboardCode;
				keys->event = KEY_MOD_UP;
				*event = EVENT_KEY_CHANGE;
				keyState = KEY_IDLE;
			}
			else
			{
				taskENTER_CRITICAL();
				tmp_timer_keypad = timer_keypad;
				taskEXIT_CRITICAL();

				if (tmp_timer_keypad == 0)
				{
					taskENTER_CRITICAL();
					timer_keypad = keypadTimerRepeat;
					taskEXIT_CRITICAL();

					keys->key = keycode;
					keys->event = KEY_MOD_LONG | KEY_MOD_DOWN;
					*event = EVENT_KEY_CHANGE;
					keyState = KEY_REPEAT;
				}
			}
			break;
		case KEY_REPEAT:
			if (keycode == 0)
			{
				keys->key = oldKeyboardCode;
				keys->event = KEY_MOD_LONG | KEY_MOD_UP;
				*event = EVENT_KEY_CHANGE;

				keyState = KEY_IDLE;
			}
			else
			{
				taskENTER_CRITICAL();
				tmp_timer_keypad = timer_keypad;
				taskEXIT_CRITICAL();

				keys->key = keycode;
				keys->event = KEY_MOD_LONG;
				*event = EVENT_KEY_CHANGE;

				if (tmp_timer_keypad == 0)
				{
					taskENTER_CRITICAL();
					timer_keypad = keypadTimerRepeat;
					taskEXIT_CRITICAL();

					if ((keys->key == KEY_LEFT) || (keys->key == KEY_RIGHT)
							|| (keys->key == KEY_UP) || (keys->key == KEY_DOWN) || (keys->key == KEY_FRONT_DOWN) || (keys->key == KEY_FRONT_UP))
					{
						keys->event = (KEY_MOD_LONG | KEY_MOD_PRESS);
					}
				}
			}
			break;
		case KEY_WAIT_RELEASED:
			if (scancode == 0)
			{
				keyState = KEY_IDLE;
			}
			break;
	}
}

/*
void buttonsFrontPanelDump(void)
{
	uint16_t keys = buttonsFrontPanelRead();

	char buffer[17];
	snprintf(buffer, sizeof(buffer), "K: 0x%03X %ld", keys, rotaryData.Count);
	displayPrintCentered(40, buffer, FONT_SIZE_3);
}
 */
