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
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "functions/sound.h"
#include "functions/sms.h"
#include "functions/settings.h"
#include "functions/codeplug.h"
#include "functions/trx.h"
#include "hardware/HR-C6000.h"
#include "functions/ticks.h"
#include "io/keyboard.h"

#define SMS_MAX_LEN        64
#define SMS_CHARS_PER_LINE 18
#define SMS_CHAR_WIDTH      8
#define SMS_VISIBLE_LINES   4
#define SMS_LINE_SPACING   10
#define SMS_TEXT_Y_START   (DISPLAY_Y_POS_MENU_START + FONT_SIZE_1_HEIGHT + 2)

enum
{
	SMS_MENU_ITEM_COMPOSE = 0,
	SMS_MENU_ITEM_INBOX,
	SMS_MENU_ITEM_QUICKTEXT,
	SMS_MENU_ITEM_SENT,
	SMS_MENU_ITEMS_COUNT
};

typedef enum
{
	SMS_QUICKTEXT_EDIT_NONE = 0,
	SMS_QUICKTEXT_EDIT_CREATE_TEXT,
	SMS_QUICKTEXT_EDIT_CREATE_TITLE,
	SMS_QUICKTEXT_EDIT_UPDATE_TEXT,
	SMS_QUICKTEXT_EDIT_UPDATE_TITLE
} smsQuickTextEditMode_t;

typedef enum
{
	SMS_VIEW_SOURCE_RX_POPUP = 0,
	SMS_VIEW_SOURCE_INBOX,
	SMS_VIEW_SOURCE_SENT
} smsViewSource_t;

enum
{
	SMS_RX_POPUP_ITEM_VIEW = 0,
	SMS_RX_POPUP_ITEM_VIEW_LATER,
	SMS_RX_POPUP_ITEM_RESPOND,
	SMS_RX_POPUP_ITEM_DELETE,
	SMS_RX_POPUP_ITEMS_COUNT
};

static char smsBuffer[SMS_MAX_LEN + 1];
static int smsCursorPos = 0;
static bool smsReplyDestinationEnabled = false;
static uint32_t smsReplyDestinationId = 0U;
static smsInboxMessage_t smsPopupMessage;
static uint8_t smsPopupMessageIndex = 0U;
static char smsPopupSource[17];
static smsViewSource_t smsViewSource = SMS_VIEW_SOURCE_RX_POPUP;
static smsInboxMessage_t smsViewInboxMessage;
static uint8_t smsViewMessageIndex = 0U;
static char smsViewPeerText[24];
static bool smsComposeHasPreset = false;
static smsQuickTextEditMode_t smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_NONE;
static uint8_t smsQuickTextEditIndex = 0U;

#define SMS_QUICKTEXT_DRAFT_TEXT  smsViewInboxMessage.text
#define SMS_QUICKTEXT_DRAFT_TITLE smsPopupSource

static const char *smsPackResultMessage(smsPackResult_t result);
static void smsOptionsRender(void);
static void smsComposeSetPreset(const char *text);
static void smsQuickTextRender(void);
static void smsQuickTextEditRender(bool fullRedraw, bool cursorMoved);
static bool smsQuickTextStartCreate(void);
static bool smsQuickTextStartEdit(uint8_t index);
static void smsDrawTextCursor(int x, int y, bool moved);

static void smsGetSourceDisplayText(uint32_t sourceId, char *buffer, size_t bufferLength)
{
	CodeplugContact_t contact;

	if ((buffer == NULL) || (bufferLength == 0U))
	{
		return;
	}

	buffer[0] = 0;

	if (codeplugContactIndexByTGorPC(sourceId, CONTACT_CALLTYPE_PC, &contact, 0) >= 0)
	{
		codeplugUtilConvertBufToString(contact.name, buffer, 16);
	}

	if (buffer[0] == 0)
	{
		snprintf(buffer, bufferLength, "%u", sourceId);
	}
}

static bool smsLoadPopupMessage(void)
{
	uint8_t count = smsGetInboxCount();

	if (count == 0U)
	{
		return false;
	}

	smsPopupMessageIndex = (uint8_t)(count - 1U);
	if (smsGetInboxMessage(smsPopupMessageIndex, &smsPopupMessage) == false)
	{
		return false;
	}

	smsGetSourceDisplayText(smsPopupMessage.sourceId, smsPopupSource, sizeof(smsPopupSource));
	return true;
}

static bool smsLoadInboxViewMessage(uint8_t index)
{
	char source[17];

	if (smsGetInboxMessage(index, &smsViewInboxMessage) == false)
	{
		return false;
	}

	smsViewSource = SMS_VIEW_SOURCE_INBOX;
	smsViewMessageIndex = index;
	smsGetSourceDisplayText(smsViewInboxMessage.sourceId, source, sizeof(source));
	snprintf(smsViewPeerText, sizeof(smsViewPeerText), "From: %s", source);
	return true;
}

static bool smsLoadSentViewMessage(uint8_t index)
{
	smsSentMessage_t message;

	if (smsGetSentMessage(index, &message) == false)
	{
		return false;
	}

	smsViewSource = SMS_VIEW_SOURCE_SENT;
	smsViewMessageIndex = index;
	snprintf(smsViewPeerText, sizeof(smsViewPeerText), "To: %u", message.destinationId);
	return true;
}

static bool smsTryResendSelectedSentMessage(void)
{
	smsPackResult_t result;
	bool waitForAckEnabled = settingsIsOptionBitSet(BIT_SMS_ACK_WAIT);
	smsSentMessage_t message;

	if (!smsGetSentMessage(smsViewMessageIndex, &message))
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Invalid message", true);
		return false;
	}

	if (trxGetMode() != RADIO_MODE_DIGITAL)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 2000, "DMR only", true);
		return false;
	}

	result = smsQueueSentMessage(smsViewMessageIndex, trxDMRID);
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

	smsRegisterOutgoingMessage(message.destinationId, trxDMRID, message.text);
	if (!waitForAckEnabled)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1800, "Sending SMS, ignoring ACK", true);
	}
	return true;
}

static bool smsGetDestinationText(char *buffer, size_t bufferLength, uint32_t *destinationId)
{
	if ((smsReplyDestinationEnabled == true) && (smsReplyDestinationId != 0U))
	{
		if (destinationId != NULL)
		{
			*destinationId = smsReplyDestinationId;
		}

		snprintf(buffer, bufferLength, "PC %u", smsReplyDestinationId);
		return true;
	}

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
		case SMS_PACK_ERROR_INVALID_INDEX:
			return "Invalid message";
		default:
			return "SMS error";
	}
}

static void smsComposeSetPreset(const char *text)
{
	if (text == NULL)
	{
		smsComposeHasPreset = false;
		return;
	}

	strncpy(smsBuffer, text, SMS_MAX_LEN);
	smsBuffer[SMS_MAX_LEN] = 0;
	smsCursorPos = strlen(smsBuffer);
	smsComposeHasPreset = true;
}

static void smsMenuRender(void)
{
	int mNum = 0;
	const char *menuText[SMS_MENU_ITEMS_COUNT] = { "SEND SMS", "INBOX", "QUICK TEXT", "SENT" };

	displayClearBuf();
	menuDisplayTitle("SMS");

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		mNum = menuGetMenuOffset(SMS_MENU_ITEMS_COUNT, i);
		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}
		menuDisplayEntry(i, mNum, menuText[mNum], 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayRender();
}

static void smsQuickTextRender(void)
{
	char line[SCREEN_LINE_BUFFER_SIZE];
	uint8_t count = smsGetQuickTextCount();

	displayClearBuf();
	menuDisplayTitle("Quick text");

	if (count == 0U)
	{
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_2_HEIGHT, "No templates", FONT_SIZE_2);
		displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "0 new", FONT_SIZE_1);
		displayThemeResetToDefault();
		displayRender();
		return;
	}

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(count, i);
		smsQuickTextMessage_t msg;

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (smsGetQuickTextMessage((uint8_t)mNum, &msg))
		{
			snprintf(line, sizeof(line), "%u %.16s", (unsigned int)(mNum + 1), msg.title);
		}
		else
		{
			strncpy(line, "Invalid", sizeof(line));
			line[sizeof(line) - 1] = 0;
		}

		menuDisplayEntry(i, mNum, line, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "0 new  # del  Green use", FONT_SIZE_1);
	displayThemeResetToDefault();
	displayRender();
}

static void smsQuickTextEditRender(bool fullRedraw, bool cursorMoved)
{
	char lineBuf[SMS_CHARS_PER_LINE + 1];
	int len = strlen(smsBuffer);
	int maxLen = SMS_MAX_LEN;
	int cursorLine;
	int cursorCol;
	int topLine;
	const char *title = "Quick text";
	const char *subtitle = "Message";
	const char *footer = "Green next  Red back";

	if ((smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_CREATE_TITLE) || (smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_UPDATE_TITLE))
	{
		title = "Quick title";
		subtitle = "Title";
		footer = "Green save  Red back";
		maxLen = SMS_QUICKTEXT_MAX_TITLE_LENGTH;
	}

	if (smsCursorPos > len)
	{
		smsCursorPos = len;
	}

	cursorLine = smsCursorPos / SMS_CHARS_PER_LINE;
	cursorCol  = smsCursorPos % SMS_CHARS_PER_LINE;

	if (cursorLine < SMS_VISIBLE_LINES)
	{
		topLine = 0;
	}
	else
	{
		topLine = cursorLine - SMS_VISIBLE_LINES + 1;
	}

	if (fullRedraw)
	{
		displayClearBuf();
		menuDisplayTitle(title);
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START, subtitle, FONT_SIZE_1);

		for (int i = 0; i < SMS_VISIBLE_LINES; i++)
		{
			int lineNum = topLine + i;
			int charStart = lineNum * SMS_CHARS_PER_LINE;
			int lineY = SMS_TEXT_Y_START + (i * SMS_LINE_SPACING);
			int charsOnLine;

			if (charStart > len)
			{
				break;
			}

			charsOnLine = len - charStart;
			if (charsOnLine > SMS_CHARS_PER_LINE)
			{
				charsOnLine = SMS_CHARS_PER_LINE;
			}

			memcpy(lineBuf, &smsBuffer[charStart], charsOnLine);
			lineBuf[charsOnLine] = 0;
			displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, lineY, lineBuf, FONT_SIZE_2);
		}

		displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT, footer, FONT_SIZE_1);
		displayThemeResetToDefault();
		displayRender();
	}

	smsDrawTextCursor(
		DISPLAY_X_POS_MENU_TEXT_OFFSET + (cursorCol * SMS_CHAR_WIDTH),
		SMS_TEXT_Y_START + ((cursorLine - topLine) * SMS_LINE_SPACING),
		cursorMoved);

	if ((int)strlen(smsBuffer) > maxLen)
	{
		smsBuffer[maxLen] = 0;
		smsCursorPos = maxLen;
	}
}

static bool smsQuickTextStartCreate(void)
{
	if (smsGetQuickTextCount() >= SMS_QUICKTEXT_MAX_MESSAGES)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1500, "Quick text full", true);
		return false;
	}

	memset(smsBuffer, 0, sizeof(smsBuffer));
	memset(SMS_QUICKTEXT_DRAFT_TEXT, 0, (SMS_MAX_LEN + 1));
	memset(SMS_QUICKTEXT_DRAFT_TITLE, 0, (SMS_QUICKTEXT_MAX_TITLE_LENGTH + 1U));
	smsCursorPos = 0;
	smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_CREATE_TEXT;
	smsQuickTextEditIndex = 0U;
	return true;
}

static bool smsQuickTextStartEdit(uint8_t index)
{
	smsQuickTextMessage_t msg;

	if (!smsGetQuickTextMessage(index, &msg))
	{
		return false;
	}

	strncpy(smsBuffer, msg.text, SMS_MAX_LEN);
	smsBuffer[SMS_MAX_LEN] = 0;
	strncpy(SMS_QUICKTEXT_DRAFT_TEXT, msg.text, SMS_MAX_LEN);
	SMS_QUICKTEXT_DRAFT_TEXT[SMS_MAX_LEN] = 0;
	strncpy(SMS_QUICKTEXT_DRAFT_TITLE, msg.title, SMS_QUICKTEXT_MAX_TITLE_LENGTH);
	SMS_QUICKTEXT_DRAFT_TITLE[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
	smsCursorPos = strlen(smsBuffer);
	smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_UPDATE_TEXT;
	smsQuickTextEditIndex = index;
	return true;
}

static void smsRxPopupRender(void)
{
	const char *menuText[SMS_RX_POPUP_ITEMS_COUNT] = { "VIEW", "VIEW LATER", "RESPOND", "DELETE" };
	char title[SCREEN_LINE_BUFFER_SIZE];

	snprintf(title, sizeof(title), "New SMS from %s", smsPopupSource);

	displayClearBuf();
	menuDisplayTitle(title);

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(SMS_RX_POPUP_ITEMS_COUNT, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}
		menuDisplayEntry(i, mNum, menuText[mNum], 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayRender();
}

static void smsViewRender(void)
{
	const char *messageText = "";
	char line2[SMS_CHARS_PER_LINE + 1];
	char line3[SMS_CHARS_PER_LINE + 1];
	smsSentMessage_t message;

	if (smsViewSource == SMS_VIEW_SOURCE_SENT)
	{
		if (smsGetSentMessage(smsViewMessageIndex, &message))
		{
			messageText = message.text;
		}
	}
	else
	{
		messageText = smsViewInboxMessage.text;
	}

	strncpy(line2, messageText, SMS_CHARS_PER_LINE);
	line2[SMS_CHARS_PER_LINE] = 0;
	strncpy(line3, &messageText[SMS_CHARS_PER_LINE], SMS_CHARS_PER_LINE);
	line3[SMS_CHARS_PER_LINE] = 0;

	displayClearBuf();
	menuDisplayTitle("SMS VIEW");
	displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START, smsViewPeerText, FONT_SIZE_1);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_1_HEIGHT + 2, line2, FONT_SIZE_2);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_1_HEIGHT + FONT_SIZE_2_HEIGHT + 6, line3, FONT_SIZE_2);
	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	if (smsViewSource == SMS_VIEW_SOURCE_SENT)
	{
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Hold 6 resend  # del", FONT_SIZE_1);
	}
	else if (smsViewSource == SMS_VIEW_SOURCE_INBOX)
	{
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Red back  # del", FONT_SIZE_1);
	}
	else
	{
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Red back", FONT_SIZE_1);
	}
	displayThemeResetToDefault();
	displayRender();
}

static void smsInboxRender(void)
{
	char line[SCREEN_LINE_BUFFER_SIZE];
	uint8_t count = smsGetInboxCount();

	displayClearBuf();
	menuDisplayTitle("SMS Inbox");

	if (count == 0U)
	{
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_2_HEIGHT, "No messages", FONT_SIZE_2);
		displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Green view # del", FONT_SIZE_1);
		displayThemeResetToDefault();
		displayRender();
		return;
	}

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(count, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		smsInboxMessage_t msg;

		if (smsGetInboxMessage((uint8_t)mNum, &msg))
		{
			snprintf(line, sizeof(line), "%u %.14s", msg.sourceId, msg.text);
		}
		else
		{
			strncpy(line, "Invalid", sizeof(line));
			line[sizeof(line) - 1] = 0;
		}

		menuDisplayEntry(i, mNum, line, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Green view # del", FONT_SIZE_1);
	displayThemeResetToDefault();

	displayRender();
}

static void smsSentRender(void)
{
	char line[SCREEN_LINE_BUFFER_SIZE];
	uint8_t count = smsGetSentCount();

	displayClearBuf();
	menuDisplayTitle("SMS Sent");

	if (count == 0U)
	{
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_2_HEIGHT, "No messages", FONT_SIZE_2);
		displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Green view # del", FONT_SIZE_1);
		displayThemeResetToDefault();
		displayRender();
		return;
	}

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(count, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		smsSentMessage_t msg;

		if (smsGetSentMessage((uint8_t)mNum, &msg))
		{
			snprintf(line, sizeof(line), "%u %.14s", msg.destinationId, msg.text);
		}
		else
		{
			strncpy(line, "Invalid", sizeof(line));
			line[sizeof(line) - 1] = 0;
		}

		menuDisplayEntry(i, mNum, line, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "Green view # del", FONT_SIZE_1);
	displayThemeResetToDefault();
	displayRender();
}

static void smsDrawTextCursor(int x, int y, bool moved)
{
	static uint32_t lastBlink = 0;
	static bool     blink = false;
	uint32_t        m = ticksGetMillis();

	if (moved)
	{
		blink = true;
	}

	if (moved || (m - lastBlink) > 500U)
	{
		displayDrawFastHLine(x, y + FONT_SIZE_2_HEIGHT, SMS_CHAR_WIDTH, blink);
		blink = !blink;
		lastBlink = m;
		displayRenderRows((y + FONT_SIZE_2_HEIGHT) / 8, (y + FONT_SIZE_2_HEIGHT) / 8 + 1);
	}
}

static void smsComposeRender(bool fullRedraw, bool cursorMoved)
{
	char destination[SCREEN_LINE_BUFFER_SIZE];
	char lineBuf[SMS_CHARS_PER_LINE + 1];
	int len = strlen(smsBuffer);
	int cursorLine, cursorCol, topLine;

	(void)smsGetDestinationText(destination, sizeof(destination), NULL);

	if (smsCursorPos > len)
	{
		smsCursorPos = len;
	}

	cursorLine = smsCursorPos / SMS_CHARS_PER_LINE;
	cursorCol  = smsCursorPos % SMS_CHARS_PER_LINE;

	if (cursorLine < SMS_VISIBLE_LINES)
	{
		topLine = 0;
	}
	else
	{
		topLine = cursorLine - SMS_VISIBLE_LINES + 1;
	}

	if (fullRedraw)
	{
		displayClearBuf();
		menuDisplayTitle("SMS");
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START, destination, FONT_SIZE_1);

		for (int i = 0; i < SMS_VISIBLE_LINES; i++)
		{
			int lineNum   = topLine + i;
			int charStart = lineNum * SMS_CHARS_PER_LINE;
			int lineY     = SMS_TEXT_Y_START + (i * SMS_LINE_SPACING);
			int charsOnLine;

			if (charStart > len)
			{
				break;
			}

			charsOnLine = len - charStart;
			if (charsOnLine > SMS_CHARS_PER_LINE)
			{
				charsOnLine = SMS_CHARS_PER_LINE;
			}

			memcpy(lineBuf, &smsBuffer[charStart], charsOnLine);
			lineBuf[charsOnLine] = 0;
			displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, lineY, lineBuf, FONT_SIZE_2);
		}

		displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT, "Green send  Red back", FONT_SIZE_1);
		displayThemeResetToDefault();
		displayRender();
	}

	smsDrawTextCursor(
		DISPLAY_X_POS_MENU_TEXT_OFFSET + (cursorCol * SMS_CHAR_WIDTH),
		SMS_TEXT_Y_START + ((cursorLine - topLine) * SMS_LINE_SPACING),
		cursorMoved);
}

static void smsComposeInsertChar(char c, bool advance, int maxLen)
{
	int len = strlen(smsBuffer);

	if (smsCursorPos >= maxLen)
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

	if (advance && (smsCursorPos < (int)strlen(smsBuffer)) && (smsCursorPos < (maxLen - 1)))
	{
		smsCursorPos++;
	}
}

static bool smsSendBuffer(void)
{
	uint32_t destinationId;
	char destination[SCREEN_LINE_BUFFER_SIZE];
	smsPackResult_t result;
	bool waitForAckEnabled = settingsIsOptionBitSet(BIT_SMS_ACK_WAIT);

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

	(void)smsStoreSentMessage(destinationId, smsBuffer);
	smsRegisterOutgoingMessage(destinationId, trxDMRID, smsBuffer);
	if (!waitForAckEnabled)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1800, "Sending SMS, ignoring ACK", true);
	}
	return true;
}

menuStatus_t menuSMSMenu(uiEvent_t *ev, bool isFirstRun)
{
	menuStatus_t menuStatus = (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);

	if (isFirstRun)
	{
		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = SMS_MENU_ITEMS_COUNT;
		smsMenuRender();
		return menuStatus;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsMenuRender();
		return menuStatus;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, SMS_MENU_ITEMS_COUNT);
		smsMenuRender();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, SMS_MENU_ITEMS_COUNT);
		smsMenuRender();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (menuDataGlobal.currentItemIndex == SMS_MENU_ITEM_COMPOSE)
		{
			smsComposeHasPreset = false;
			menuSystemPushNewMenu(MENU_SMS_COMPOSE);
		}
		else if (menuDataGlobal.currentItemIndex == SMS_MENU_ITEM_INBOX)
		{
			menuSystemPushNewMenu(MENU_SMS_INBOX);
		}
		else if (menuDataGlobal.currentItemIndex == SMS_MENU_ITEM_QUICKTEXT)
		{
			menuSystemPushNewMenu(MENU_SMS_QUICKTEXT);
		}
		else
		{
			menuSystemPushNewMenu(MENU_SMS_SENT);
		}
	}

	return menuStatus;
}

menuStatus_t menuSMSCompose(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		if (!smsComposeHasPreset)
		{
			memset(smsBuffer, 0, sizeof(smsBuffer));
			smsCursorPos = 0;
		}
		else
		{
			smsComposeHasPreset = false;
			smsCursorPos = strlen(smsBuffer);
		}
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
				smsReplyDestinationEnabled = false;
				smsReplyDestinationId = 0U;
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
		smsComposeInsertChar(' ', true, SMS_MAX_LEN);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->keys.event == KEY_MOD_PREVIEW) && (ev->keys.key >= 32) && (ev->keys.key <= 126))
	{
		smsComposeInsertChar(ev->keys.key, false, SMS_MAX_LEN);
		announceChar(ev->keys.key);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->keys.event == KEY_MOD_PRESS) && (ev->keys.key >= 32) && (ev->keys.key <= 126))
	{
		smsComposeInsertChar(ev->keys.key, true, SMS_MAX_LEN);
		announceChar(ev->keys.key);
		smsComposeRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	smsComposeRender(false, false);
	return MENU_STATUS_SUCCESS;
}

menuStatus_t menuSMSInbox(uiEvent_t *ev, bool isFirstRun)
{
	uint8_t count = smsGetInboxCount();
	menuStatus_t menuStatus = (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);

	if (isFirstRun)
	{
		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = count;
		smsInboxRender();
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsInboxRender();
		return menuStatus;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (count == 0U)
	{
		smsInboxRender();
		return menuStatus;
	}

	if (menuDataGlobal.currentItemIndex >= count)
	{
		menuDataGlobal.currentItemIndex = (count - 1U);
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, count);
		smsInboxRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, count);
		smsInboxRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (smsLoadInboxViewMessage((uint8_t)menuDataGlobal.currentItemIndex))
		{
			menuSystemPushNewMenu(MENU_SMS_VIEW);
		}
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		if (smsDeleteInboxMessage((uint8_t)menuDataGlobal.currentItemIndex))
		{
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Inbox deleted", true);
			if ((menuDataGlobal.currentItemIndex > 0) && (menuDataGlobal.currentItemIndex >= smsGetInboxCount()))
			{
				menuDataGlobal.currentItemIndex--;
			}
		}
		else
		{
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Delete failed", true);
		}

		smsInboxRender();
		return MENU_STATUS_SUCCESS;
	}

	return menuStatus;
}

menuStatus_t menuSMSSent(uiEvent_t *ev, bool isFirstRun)
{
	uint8_t count = smsGetSentCount();
	menuStatus_t menuStatus = (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);

	if (isFirstRun)
	{
		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = count;
		smsSentRender();
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsSentRender();
		return menuStatus;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (count == 0U)
	{
		smsSentRender();
		return menuStatus;
	}

	if (menuDataGlobal.currentItemIndex >= count)
	{
		menuDataGlobal.currentItemIndex = (count - 1U);
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, count);
		smsSentRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, count);
		smsSentRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		if (smsDeleteSentMessage((uint8_t)menuDataGlobal.currentItemIndex))
		{
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Sent deleted", true);
			if ((menuDataGlobal.currentItemIndex > 0) && (menuDataGlobal.currentItemIndex >= smsGetSentCount()))
			{
				menuDataGlobal.currentItemIndex--;
			}
		}
		else
		{
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Delete failed", true);
		}

		smsSentRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (smsLoadSentViewMessage((uint8_t)menuDataGlobal.currentItemIndex))
		{
			menuSystemPushNewMenu(MENU_SMS_VIEW);
		}
		return MENU_STATUS_SUCCESS;
	}

	return menuStatus;
}

menuStatus_t menuSMSQuickText(uiEvent_t *ev, bool isFirstRun)
{
	uint8_t count = smsGetQuickTextCount();
	menuStatus_t menuStatus = (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);

	if (isFirstRun)
	{
		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = count;
		smsQuickTextRender();
		return menuStatus;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsQuickTextRender();
		return menuStatus;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if ((KEYCHECK_SHORTUP(ev->keys, KEY_0) || KEYCHECK_PRESS(ev->keys, KEY_0)) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_0) == false))
	{
		if (smsQuickTextStartCreate())
		{
			menuSystemPushNewMenu(MENU_SMS_QUICKTEXT_EDIT);
		}
		return MENU_STATUS_SUCCESS;
	}

	if (count == 0U)
	{
		smsQuickTextRender();
		return menuStatus;
	}

	if (menuDataGlobal.currentItemIndex >= count)
	{
		menuDataGlobal.currentItemIndex = (count - 1U);
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, count);
		smsQuickTextRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, count);
		smsQuickTextRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		if (smsDeleteQuickTextMessage((uint8_t)menuDataGlobal.currentItemIndex))
		{
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Quick text deleted", true);
			if ((menuDataGlobal.currentItemIndex > 0) && (menuDataGlobal.currentItemIndex >= smsGetQuickTextCount()))
			{
				menuDataGlobal.currentItemIndex--;
			}
		}
		else
		{
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Delete failed", true);
		}

		smsQuickTextRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_LONGDOWN(ev->keys, KEY_3) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_3) == false))
	{
		if (smsQuickTextStartEdit((uint8_t)menuDataGlobal.currentItemIndex))
		{
			menuSystemPushNewMenu(MENU_SMS_QUICKTEXT_EDIT);
		}
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		smsQuickTextMessage_t message;

		if (smsGetQuickTextMessage((uint8_t)menuDataGlobal.currentItemIndex, &message))
		{
			smsComposeSetPreset(message.text);
			menuSystemPushNewMenu(MENU_SMS_COMPOSE);
		}
		return MENU_STATUS_SUCCESS;
	}

	return menuStatus;
}

menuStatus_t menuSMSQuickTextEdit(uiEvent_t *ev, bool isFirstRun)
{
	int maxLen = (((smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_CREATE_TITLE) || (smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_UPDATE_TITLE)) ? SMS_QUICKTEXT_MAX_TITLE_LENGTH : SMS_MAX_LEN);

	if (isFirstRun)
	{
		keypadAlphaEnable = true;
		smsQuickTextEditRender(true, true);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsQuickTextEditRender(true, false);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_CREATE_TITLE)
		{
			smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_CREATE_TEXT;
			strncpy(smsBuffer, SMS_QUICKTEXT_DRAFT_TEXT, SMS_MAX_LEN);
			smsBuffer[SMS_MAX_LEN] = 0;
			smsCursorPos = strlen(smsBuffer);
			smsQuickTextEditRender(true, true);
			return MENU_STATUS_SUCCESS;
		}

		if (smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_UPDATE_TITLE)
		{
			smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_UPDATE_TEXT;
			strncpy(smsBuffer, SMS_QUICKTEXT_DRAFT_TEXT, SMS_MAX_LEN);
			smsBuffer[SMS_MAX_LEN] = 0;
			smsCursorPos = strlen(smsBuffer);
			smsQuickTextEditRender(true, true);
			return MENU_STATUS_SUCCESS;
		}

		keypadAlphaEnable = false;
		smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_NONE;
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (strlen(smsBuffer) == 0)
		{
			soundSetMelody(MELODY_ERROR_BEEP);
			return MENU_STATUS_SUCCESS;
		}

		if ((smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_CREATE_TEXT) || (smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_UPDATE_TEXT))
		{
			strncpy(SMS_QUICKTEXT_DRAFT_TEXT, smsBuffer, SMS_MAX_LEN);
			SMS_QUICKTEXT_DRAFT_TEXT[SMS_MAX_LEN] = 0;

			smsQuickTextEditMode = (smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_CREATE_TEXT) ? SMS_QUICKTEXT_EDIT_CREATE_TITLE : SMS_QUICKTEXT_EDIT_UPDATE_TITLE;
			strncpy(smsBuffer, SMS_QUICKTEXT_DRAFT_TITLE, SMS_QUICKTEXT_MAX_TITLE_LENGTH);
			smsBuffer[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
			smsCursorPos = strlen(smsBuffer);
			smsQuickTextEditRender(true, true);
			return MENU_STATUS_SUCCESS;
		}

		strncpy(SMS_QUICKTEXT_DRAFT_TITLE, smsBuffer, SMS_QUICKTEXT_MAX_TITLE_LENGTH);
		SMS_QUICKTEXT_DRAFT_TITLE[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;

		if (smsQuickTextEditMode == SMS_QUICKTEXT_EDIT_CREATE_TITLE)
		{
			if (smsStoreQuickTextMessage(SMS_QUICKTEXT_DRAFT_TITLE, SMS_QUICKTEXT_DRAFT_TEXT))
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Quick text saved", true);
			}
			else
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Save failed", true);
			}
		}
		else
		{
			if (smsUpdateQuickTextMessage(smsQuickTextEditIndex, SMS_QUICKTEXT_DRAFT_TITLE, SMS_QUICKTEXT_DRAFT_TEXT))
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Quick text updated", true);
			}
			else
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Update failed", true);
			}
		}

		keypadAlphaEnable = false;
		smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_NONE;
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
	{
		moveCursorLeftInString(smsBuffer, &smsCursorPos, false);
		smsQuickTextEditRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		moveCursorRightInString(smsBuffer, &smsCursorPos, maxLen, false);
		smsQuickTextEditRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		moveCursorLeftInString(smsBuffer, &smsCursorPos, true);
		smsQuickTextEditRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		smsComposeInsertChar(' ', true, maxLen);
		smsQuickTextEditRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->keys.event == KEY_MOD_PREVIEW) && (ev->keys.key >= 32) && (ev->keys.key <= 126))
	{
		smsComposeInsertChar(ev->keys.key, false, maxLen);
		announceChar(ev->keys.key);
		smsQuickTextEditRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->keys.event == KEY_MOD_PRESS) && (ev->keys.key >= 32) && (ev->keys.key <= 126))
	{
		smsComposeInsertChar(ev->keys.key, true, maxLen);
		announceChar(ev->keys.key);
		smsQuickTextEditRender(true, true);
		return MENU_STATUS_SUCCESS;
	}

	smsQuickTextEditRender(false, false);
	return MENU_STATUS_SUCCESS;
}

menuStatus_t menuSMSRxPopup(uiEvent_t *ev, bool isFirstRun)
{
	menuStatus_t menuStatus = (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);

	if (isFirstRun)
	{
		smsReplyDestinationEnabled = false;
		smsReplyDestinationId = 0U;
		(void)smsConsumeRxNotification();

		if (smsLoadPopupMessage() == false)
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		smsViewSource = SMS_VIEW_SOURCE_RX_POPUP;
		smsViewInboxMessage = smsPopupMessage;
		smsViewMessageIndex = smsPopupMessageIndex;
		snprintf(smsViewPeerText, sizeof(smsViewPeerText), "From: %s", smsPopupSource);

		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = SMS_RX_POPUP_ITEMS_COUNT;
		smsRxPopupRender();
		return menuStatus;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsRxPopupRender();
		return menuStatus;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		smsReplyDestinationEnabled = false;
		smsReplyDestinationId = 0U;
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, SMS_RX_POPUP_ITEMS_COUNT);
		smsRxPopupRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, SMS_RX_POPUP_ITEMS_COUNT);
		smsRxPopupRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		switch (menuDataGlobal.currentItemIndex)
		{
			case SMS_RX_POPUP_ITEM_VIEW:
					smsViewSource = SMS_VIEW_SOURCE_RX_POPUP;
					smsViewInboxMessage = smsPopupMessage;
					smsViewMessageIndex = smsPopupMessageIndex;
					snprintf(smsViewPeerText, sizeof(smsViewPeerText), "From: %s", smsPopupSource);
				menuSystemPushNewMenu(MENU_SMS_VIEW);
				break;

			case SMS_RX_POPUP_ITEM_VIEW_LATER:
				smsReplyDestinationEnabled = false;
				smsReplyDestinationId = 0U;
				menuSystemPopPreviousMenu();
				break;

			case SMS_RX_POPUP_ITEM_RESPOND:
				smsReplyDestinationEnabled = true;
				smsReplyDestinationId = smsPopupMessage.sourceId;
				menuSystemPushNewMenu(MENU_SMS_COMPOSE);
				break;

			case SMS_RX_POPUP_ITEM_DELETE:
				(void)smsDeleteInboxMessage(smsPopupMessageIndex);
				smsReplyDestinationEnabled = false;
				smsReplyDestinationId = 0U;
				menuSystemPopPreviousMenu();
				break;

			default:
				break;
		}

		return MENU_STATUS_SUCCESS;
	}

	return menuStatus;
}

menuStatus_t menuSMSView(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		smsViewRender();
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsViewRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (smsViewSource == SMS_VIEW_SOURCE_SENT)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			if (smsDeleteSentMessage(smsViewMessageIndex))
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Sent deleted", true);
			}
			else
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Delete failed", true);
			}

			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_LONGDOWN(ev->keys, KEY_6) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_6) == false))
		{
			(void)smsTryResendSelectedSentMessage();
			return MENU_STATUS_SUCCESS;
		}
	}
	else if (smsViewSource == SMS_VIEW_SOURCE_INBOX)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			if (smsDeleteInboxMessage(smsViewMessageIndex))
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Inbox deleted", true);
			}
			else
			{
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1200, "Delete failed", true);
			}

			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}
	}

	return MENU_STATUS_SUCCESS;
}

static void smsOptionsRender(void)
{
	char line[SCREEN_LINE_BUFFER_SIZE];
	int mNum = 0;
	const char *ackValue = (settingsIsOptionBitSet(BIT_SMS_ACK_WAIT) ? currentLanguage->on : currentLanguage->off);
	const char *filterValue = (settingsIsOptionBitSet(BIT_SMS_FILTER_INCOMING_PC) ? "PC" : "None");

	displayClearBuf();
	menuDisplayTitle("SMS options");

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		mNum = menuGetMenuOffset(menuDataGlobal.numItems, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (mNum == 0)
		{
			snprintf(line, sizeof(line), "Wait for ACK:%s", ackValue);
			menuDisplayEntry(i, mNum, line, 13, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		}
		else
		{
			snprintf(line, sizeof(line), "In filter:%s", filterValue);
			menuDisplayEntry(i, mNum, line, 10, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		}
	}

	displayRender();
}

menuStatus_t menuSMSOptions(uiEvent_t *ev, bool isFirstRun)
{
	menuStatus_t menuStatus = (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);

	if (isFirstRun)
	{
		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = 2;
		smsOptionsRender();
		return menuStatus;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		smsOptionsRender();
		return menuStatus;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, menuDataGlobal.numItems);
		smsOptionsRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, menuDataGlobal.numItems);
		smsOptionsRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT) || KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		if (menuDataGlobal.currentItemIndex == 0)
		{
			settingsSetOptionBit(BIT_SMS_ACK_WAIT, !settingsIsOptionBitSet(BIT_SMS_ACK_WAIT));
		}
		else
		{
			settingsSetOptionBit(BIT_SMS_FILTER_INCOMING_PC, !settingsIsOptionBitSet(BIT_SMS_FILTER_INCOMING_PC));
		}

		smsOptionsRender();
		return MENU_STATUS_SUCCESS;
	}

	return menuStatus;
}
