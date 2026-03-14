/*
 * Copyright (C) 2024
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

#include <string.h>
#include <stdio.h>

#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "functions/sound.h"
#include "functions/sms.h"
#include "functions/trx.h"
#include "hardware/HR-C6000.h"
#include "io/keyboard.h"

#define SMS_MAX_LEN 64
#define SMS_VISIBLE_CHARS 18

static char smsBuffer[SMS_MAX_LEN + 1];
static int smsCursorPos = 0;

static bool smsGetDestinationText(char *buffer, size_t bufferLength, uint32_t *destinationId)
{
	if (((trxTalkGroupOrPcId >> 24) == PC_CALL_FLAG) && ((trxTalkGroupOrPcId & 0x00FFFFFFU) != 0U))
	{
		uint32_t pcId = (trxTalkGroupOrPcId & 0x00FFFFFFU);
		if (destinationId != NULL)
		{
			*destinationId = pcId;
		}
		snprintf(buffer, bufferLength, "PC %u", pcId);
		return true;
	}

	if (destinationId != NULL)
	{
		*destinationId = 0U;
	}
	snprintf(buffer, bufferLength, "Select private call");
	return false;
}

static const char *smsPackResultMessage(smsPackResult_t result)
{
	switch (result)
	{
		case SMS_PACK_OK:
			return "SMS queued";
		case SMS_PACK_ERROR_EMPTY:
			return "Empty message";
		case SMS_PACK_ERROR_TOO_LONG:
			return "Message too long";
		case SMS_PACK_ERROR_INVALID_DEST:
			return "Select private call";
		case SMS_PACK_ERROR_INVALID_SRC:
			return "Invalid DMR ID";
		case SMS_PACK_ERROR_UNSUPPORTED_CHAR:
			return "ASCII only";
		default:
			return "SMS error";
	}
}

static void smsMenuRender(void)
{
	int mNum = 0;

	displayClearBuf();
	menuDisplayTitle("SMS");

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		if (menuGetMenuOffset(1, i) != MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			menuDisplayEntry(i, mNum, "Sent message", 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
			break;
		}
	}

	displayRender();
}

static void smsComposeRender(bool fullRedraw, bool cursorMoved)
{
	char visible[SMS_VISIBLE_CHARS + 1];
	char destination[SCREEN_LINE_BUFFER_SIZE];
	int len = strlen(smsBuffer);
	int start = 0;
	(void)smsGetDestinationText(destination, sizeof(destination), NULL);

	if (smsCursorPos > len)
	{
		smsCursorPos = len;
	}

	if (smsCursorPos >= SMS_VISIBLE_CHARS)
	{
		start = (smsCursorPos - SMS_VISIBLE_CHARS) + 1;
	}

	memset(visible, 0, sizeof(visible));
	strncpy(visible, &smsBuffer[start], SMS_VISIBLE_CHARS);

	if (fullRedraw)
	{
		displayClearBuf();
		menuDisplayTitle("SMS");
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START, "Sent message", FONT_SIZE_2);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_2_HEIGHT + 2, destination, FONT_SIZE_1);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + (MENU_ENTRY_HEIGHT * 2), visible, FONT_SIZE_2);
		displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Green send  Red back", FONT_SIZE_1);
		displayThemeResetToDefault();
	}

	menuUpdateCursor((smsCursorPos - start), cursorMoved, false);
	displayRender();
}

static void smsComposeInsertChar(char c, bool advance)
{
	int len = strlen(smsBuffer);

	if (smsCursorPos >= SMS_MAX_LEN)
	{
		return;
	}

	if (smsCursorPos == len)
	{
		smsBuffer[smsCursorPos] = c;
		smsBuffer[smsCursorPos + 1] = 0;
	}
	else
	{
		smsBuffer[smsCursorPos] = c;
	}

	if (advance && (smsCursorPos < SMS_MAX_LEN))
	{
		smsCursorPos++;
	}
}

static bool smsSendBuffer(void)
{
	uint32_t destinationId;
	char destination[SCREEN_LINE_BUFFER_SIZE];
	smsPackResult_t result;

	if (trxGetMode() != RADIO_MODE_DIGITAL)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 2000, "DMR only", true);
		return false;
	}

	if (smsGetDestinationText(destination, sizeof(destination), &destinationId) == false)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 2000, "Select private call", true);
		return false;
	}

	result = smsQueueMessage(destinationId, trxDMRID, smsBuffer);
	if (result != SMS_PACK_OK)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 2000, smsPackResultMessage(result), true);
		return false;
	}

	if (HRC6000StartQueuedSMS() == false)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 2000, "SMS busy", true);
		return false;
	}

	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1500, "SMS TX", true);
	return true;
}

menuStatus_t menuSMSMenu(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = 1;
		smsMenuRender();
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsMenuRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuSystemPushNewMenu(MENU_SMS_COMPOSE);
	}

	return MENU_STATUS_SUCCESS;
}

menuStatus_t menuSMSCompose(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		memset(smsBuffer, 0, sizeof(smsBuffer));
		smsCursorPos = 0;
		smsClearQueuedMessage();
		keypadAlphaEnable = true;
		smsComposeRender(true, true);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsComposeRender(true, false);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		keypadAlphaEnable = false;
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (strlen(smsBuffer) == 0)
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
		else
		{
			if (smsSendBuffer())
			{
				keypadAlphaEnable = false;
				menuSystemPopAllAndDisplayRootMenu();
			}
		}

		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
	{
		moveCursorLeftInString(smsBuffer, &smsCursorPos, false);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		moveCursorRightInString(smsBuffer, &smsCursorPos, SMS_MAX_LEN, false);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		moveCursorLeftInString(smsBuffer, &smsCursorPos, true);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		smsComposeInsertChar(' ', true);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->keys.event == KEY_MOD_PREVIEW) && (ev->keys.key >= 32) && (ev->keys.key <= 126))
	{
		smsComposeInsertChar(ev->keys.key, false);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->keys.event == KEY_MOD_PRESS) && (ev->keys.key >= 32) && (ev->keys.key <= 126))
	{
		smsComposeInsertChar(ev->keys.key, true);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	smsComposeRender(false, false);
	return MENU_STATUS_SUCCESS;
}
