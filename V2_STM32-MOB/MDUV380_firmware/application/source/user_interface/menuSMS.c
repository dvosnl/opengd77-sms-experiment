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
#include <stdlib.h>

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

#define SMS_MAX_LEN        SMS_MAX_TEXT_LENGTH
#define SMS_CHARS_PER_LINE 18
#define SMS_CHAR_WIDTH      8
#define SMS_VISIBLE_LINES   4
#define SMS_LINE_SPACING   10
#define SMS_TEXT_Y_START   (DISPLAY_Y_POS_MENU_START + FONT_SIZE_1_HEIGHT + 2)
#define SMS_VIEW_CHARS_PER_LINE 18
#define SMS_VIEW_VISIBLE_LINES   4
#define SMS_VIEW_LINE_SPACING   18
#define SMS_VIEW_TEXT_Y_START   (DISPLAY_Y_POS_MENU_START + FONT_SIZE_1_HEIGHT + 2)
#define SMS_VIEW_SCROLLBAR_WIDTH 7
#define SMS_VIEW_SCROLLBAR_X    (DISPLAY_SIZE_X - SMS_VIEW_SCROLLBAR_WIDTH)
#define SMS_VIEW_MAX_LINES      (SMS_MAX_LEN + 2)

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

typedef enum
{
	SMS_COMPOSE_MODE_EDIT = 0,
	SMS_COMPOSE_MODE_DESTINATION_SELECT,
	SMS_COMPOSE_MODE_CONTACT_SELECT,
	SMS_COMPOSE_MODE_MANUAL_ID
} smsComposeMode_t;

typedef enum
{
	SMS_DESTINATION_OPTION_SELECT_CONTACT = 0,
	SMS_DESTINATION_OPTION_MANUAL_ID,
	SMS_DESTINATION_OPTION_COUNT
} smsDestinationOption_t;

typedef enum
{
	SMS_RESPOND_OPTION_SELECT_CONTACT = 0,
	SMS_RESPOND_OPTION_TO_SENDER,
	SMS_RESPOND_OPTION_COUNT
} smsRespondOption_t;

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
static uint8_t smsViewTopLine = 0U;
static bool smsComposeHasPreset = false;
static smsQuickTextEditMode_t smsQuickTextEditMode = SMS_QUICKTEXT_EDIT_NONE;
static uint8_t smsQuickTextEditIndex = 0U;
static smsComposeMode_t smsComposeMode = SMS_COMPOSE_MODE_EDIT;
static uint16_t smsComposeContactIndex = 0U;
static uint8_t smsComposeDestinationOptionIndex = 0U;
static char smsComposeManualIdBuffer[11] = { 0 };
static bool smsRxRespondMode = false;
static uint8_t smsRxRespondOptionIndex = 0U;

#define SMS_QUICKTEXT_DRAFT_TEXT  smsViewInboxMessage.text
#define SMS_QUICKTEXT_DRAFT_TITLE smsPopupSource

static const char *smsPackResultMessage(smsPackResult_t result);
static void smsOptionsRender(void);
static void smsComposeSetPreset(const char *text);
static void smsComposeRenderDestinationSelect(void);
static void smsComposeRenderContactSelect(void);
static void smsComposeRenderManualIdEntry(void);
static bool smsComposeGetPrivateContactAt(uint16_t listIndex, CodeplugContact_t *contact);
static void smsQuickTextRender(void);
static void smsQuickTextEditRender(bool fullRedraw, bool cursorMoved);
static bool smsQuickTextStartCreate(void);
static bool smsQuickTextStartEdit(uint8_t index);
static void smsDrawTextCursor(int x, int y, bool moved);
static void smsRxRespondRender(void);
static uint16_t smsBuildViewText(const char *source, char *destination, uint16_t destinationSize);
static int smsBuildViewLineLayout(const char *messageText, uint16_t messageLength, uint16_t *lineStarts, uint8_t *lineLengths, int maxLines);
static void smsBuildMessagePreview(const char *source, char *destination, uint16_t destinationSize, uint16_t maxChars);

static uint16_t smsBuildViewText(const char *source, char *destination, uint16_t destinationSize)
{
	uint16_t outIndex = 0U;
	bool previousWasSpace = false;

	if ((source == NULL) || (destination == NULL) || (destinationSize == 0U))
	{
		return 0U;
	}

	for (uint16_t i = 0U; (source[i] != 0) && (outIndex < (uint16_t)(destinationSize - 1U)); i++)
	{
		char c = source[i];

		if (c == '\r')
		{
			continue;
		}

		if (c == '\n')
		{
			while ((outIndex > 0U) && (destination[outIndex - 1U] == ' '))
			{
				outIndex--;
			}

			if ((outIndex == 0U) || (destination[outIndex - 1U] != '\n'))
			{
				destination[outIndex++] = '\n';
			}

			previousWasSpace = false;
			continue;
		}

		if ((c == '\t') || (((uint8_t)c) < 0x20U) || (((uint8_t)c) == 0x7FU))
		{
			c = ' ';
		}

		if (c == ' ')
		{
			if (previousWasSpace)
			{
				continue;
			}
			previousWasSpace = true;
		}
		else
		{
			previousWasSpace = false;
		}

		destination[outIndex++] = c;
	}

	while ((outIndex > 0U) && ((destination[outIndex - 1U] == ' ') || (destination[outIndex - 1U] == '\n')))
	{
		outIndex--;
	}

	destination[outIndex] = 0;
	return outIndex;
}

static int smsBuildViewLineLayout(const char *messageText, uint16_t messageLength, uint16_t *lineStarts, uint8_t *lineLengths, int maxLines)
{
	int lineCount = 0;
	uint16_t cursor = 0U;

	if ((lineStarts == NULL) || (lineLengths == NULL) || (maxLines <= 0))
	{
		return 1;
	}

	if (messageLength == 0U)
	{
		lineStarts[0] = 0U;
		lineLengths[0] = 0U;
		return 1;
	}

	while ((cursor < messageLength) && (lineCount < maxLines))
	{
		uint16_t start = cursor;
		uint16_t end;
		int32_t lastSpace = -1;
		uint8_t lineLength = 0U;

		if (messageText[cursor] == '\n')
		{
			lineStarts[lineCount] = (uint16_t)(cursor + 1U);
			lineLengths[lineCount] = 0U;
			lineCount++;
			cursor++;
			continue;
		}

		end = cursor;
		while ((end < messageLength) && (messageText[end] != '\n') && ((end - start) < SMS_VIEW_CHARS_PER_LINE))
		{
			if (messageText[end] == ' ')
			{
				lastSpace = (int32_t)end;
			}
			end++;
		}

		if ((end < messageLength) && (messageText[end] == '\n'))
		{
			lineLength = (uint8_t)(end - start);
			while ((lineLength > 0U) && (messageText[start + lineLength - 1U] == ' '))
			{
				lineLength--;
			}

			lineStarts[lineCount] = start;
			lineLengths[lineCount] = lineLength;
			lineCount++;
			cursor = (uint16_t)(end + 1U);
			while ((cursor < messageLength) && (messageText[cursor] == ' '))
			{
				cursor++;
			}
			continue;
		}

		if ((end - start) < SMS_VIEW_CHARS_PER_LINE)
		{
			lineLength = (uint8_t)(end - start);
			while ((lineLength > 0U) && (messageText[start + lineLength - 1U] == ' '))
			{
				lineLength--;
			}

			lineStarts[lineCount] = start;
			lineLengths[lineCount] = lineLength;
			lineCount++;
			cursor = end;
			continue;
		}

		if (lastSpace >= (int32_t)start)
		{
			lineLength = (uint8_t)((uint16_t)lastSpace - start);
			while ((lineLength > 0U) && (messageText[start + lineLength - 1U] == ' '))
			{
				lineLength--;
			}

			lineStarts[lineCount] = start;
			lineLengths[lineCount] = lineLength;
			lineCount++;
			cursor = (uint16_t)((uint16_t)lastSpace + 1U);
			while ((cursor < messageLength) && (messageText[cursor] == ' '))
			{
				cursor++;
			}
		}
		else
		{
			lineStarts[lineCount] = start;
			lineLengths[lineCount] = SMS_VIEW_CHARS_PER_LINE;
			lineCount++;
			cursor = end;
		}
	}

	if (lineCount == 0)
	{
		lineStarts[0] = 0U;
		lineLengths[0] = 0U;
		lineCount = 1;
	}

	return lineCount;
}

static void smsBuildMessagePreview(const char *source, char *destination, uint16_t destinationSize, uint16_t maxChars)
{
	uint16_t outIndex = 0U;

	if ((source == NULL) || (destination == NULL) || (destinationSize == 0U))
	{
		return;
	}

	for (uint16_t i = 0U; (source[i] != 0) && (outIndex < maxChars) && (outIndex < (uint16_t)(destinationSize - 1U)); i++)
	{
		char c = source[i];

		if ((c == '\r') || (c == '\n') || (c == '\t') || (((uint8_t)c) < 0x20U) || (((uint8_t)c) == 0x7FU))
		{
			c = ' ';
		}

		if ((c == ' ') && ((outIndex == 0U) || (destination[outIndex - 1U] == ' ')))
		{
			continue;
		}

		destination[outIndex++] = c;
	}

	destination[outIndex] = 0;
}

static int smsViewGetVisibleLines(void)
{
	int textAreaHeight = (DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT - SMS_VIEW_TEXT_Y_START);
	int lines = 1;

	if (textAreaHeight > FONT_SIZE_3_HEIGHT)
	{
		lines = 1 + ((textAreaHeight - FONT_SIZE_3_HEIGHT) / SMS_VIEW_LINE_SPACING);
	}

	if (lines > SMS_VIEW_VISIBLE_LINES)
	{
		lines = SMS_VIEW_VISIBLE_LINES;
	}

	if (lines < 1)
	{
		lines = 1;
	}

	return lines;
}

static void smsViewDrawScrollbar(int totalLines, int visibleLines, int maxTopLine)
{
	int trackTop = SMS_VIEW_TEXT_Y_START;
	int trackHeight = ((DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT - 2) - trackTop);
	int thumbHeight;
	int thumbY = trackTop;

	if (trackHeight <= 0)
	{
		return;
	}

	displayDrawFastVLine(SMS_VIEW_SCROLLBAR_X, trackTop, trackHeight, true);
	displayDrawFastVLine((SMS_VIEW_SCROLLBAR_X + SMS_VIEW_SCROLLBAR_WIDTH - 1), trackTop, trackHeight, true);

	if ((totalLines <= visibleLines) || (maxTopLine <= 0))
	{
		thumbHeight = trackHeight;
	}
	else
	{
		thumbHeight = (visibleLines * trackHeight) / totalLines;
		if (thumbHeight < 6)
		{
			thumbHeight = 6;
		}
		if (thumbHeight > trackHeight)
		{
			thumbHeight = trackHeight;
		}

		thumbY = trackTop + ((((int)smsViewTopLine) * (trackHeight - thumbHeight)) / maxTopLine);
	}

	displayFillRect(SMS_VIEW_SCROLLBAR_X, thumbY, SMS_VIEW_SCROLLBAR_WIDTH, thumbHeight, true);
}

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
	smsViewTopLine = 0U;
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
	smsViewTopLine = 0U;
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

	if (waitForAckEnabled)
	{
		smsRegisterOutgoingMessage(message.destinationId, trxDMRID, message.text);
	}
	else
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1800, "Sending SMS, ignoring ACK", true);
	}
	return true;
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
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "0 new  Green use", FONT_SIZE_1);
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

static void smsRxRespondRender(void)
{
	const char *menuText[SMS_RESPOND_OPTION_COUNT] = { "Select contact", "To sender" };

	menuDataGlobal.numItems = SMS_RESPOND_OPTION_COUNT;
	if (smsRxRespondOptionIndex >= SMS_RESPOND_OPTION_COUNT)
	{
		smsRxRespondOptionIndex = 0U;
	}
	menuDataGlobal.currentItemIndex = smsRxRespondOptionIndex;

	displayClearBuf();
	menuDisplayTitle("Respond");

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(SMS_RESPOND_OPTION_COUNT, i);

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

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT, "Green select  Red back", FONT_SIZE_1);
	displayThemeResetToDefault();
	displayRender();
}

static void smsViewRender(void)
{
	const char *messageText = "";
	char displayText[SMS_MAX_LEN + 1U];
	char lineBuf[SMS_VIEW_CHARS_PER_LINE + 1];
	uint16_t lineStarts[SMS_VIEW_MAX_LINES];
	uint8_t lineLengths[SMS_VIEW_MAX_LINES];
	uint16_t messageLength;
	int totalLines;
	int maxTopLine;
	int visibleLines = smsViewGetVisibleLines();

	if (smsViewSource == SMS_VIEW_SOURCE_SENT)
	{
		smsSentMessage_t message;

		if (smsGetSentMessage(smsViewMessageIndex, &message))
		{
			messageText = message.text;
		}
	}
	else
	{
		messageText = smsViewInboxMessage.text;
	}

	messageLength = smsBuildViewText(messageText, displayText, sizeof(displayText));
	totalLines = smsBuildViewLineLayout(displayText, messageLength, lineStarts, lineLengths, SMS_VIEW_MAX_LINES);

	maxTopLine = (totalLines > visibleLines) ? (totalLines - visibleLines) : 0;
	if (smsViewTopLine > maxTopLine)
	{
		smsViewTopLine = maxTopLine;
	}

	displayClearBuf();
	menuDisplayTitle("SMS VIEW");
	displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START, smsViewPeerText, FONT_SIZE_1);

	for (int i = 0; i < visibleLines; i++)
	{
		int lineIndex = ((int)smsViewTopLine + i);
		uint16_t charStart;
		uint16_t charsOnLine;
		int y = SMS_VIEW_TEXT_Y_START + (i * SMS_VIEW_LINE_SPACING);

		if (lineIndex >= totalLines)
		{
			break;
		}

		charStart = lineStarts[lineIndex];
		charsOnLine = lineLengths[lineIndex];

		if (charsOnLine == 0U)
		{
			continue;
		}

		memcpy(lineBuf, &displayText[charStart], charsOnLine);
		lineBuf[charsOnLine] = 0;
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, y, lineBuf, FONT_SIZE_3);
	}

	smsViewDrawScrollbar(totalLines, visibleLines, maxTopLine);

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	if (smsViewSource == SMS_VIEW_SOURCE_SENT)
	{
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "6 resend  # del", FONT_SIZE_1);
	}
	else if (smsViewSource == SMS_VIEW_SOURCE_INBOX)
	{
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT, "# delete", FONT_SIZE_1);
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
	int firstIndex = 0;
	int visibleEntries = (MENU_END_ITERATION_VALUE - MENU_START_ITERATION_VALUE + 1);

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

	if (menuDataGlobal.currentItemIndex >= count)
	{
		menuDataGlobal.currentItemIndex = (count - 1U);
	}

	if (menuDataGlobal.currentItemIndex >= (uint8_t)(visibleEntries - 1))
	{
		firstIndex = (int)menuDataGlobal.currentItemIndex - (visibleEntries - 1);
	}

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = firstIndex + (i - MENU_START_ITERATION_VALUE);

		if (mNum >= count)
		{
			break;
		}

		smsInboxMessage_t msg;

		if (smsGetInboxMessage((uint8_t)mNum, &msg))
		{
			char preview[15];
			smsBuildMessagePreview(msg.text, preview, sizeof(preview), 14U);
			snprintf(line, sizeof(line), "%u %s", msg.sourceId, preview);
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
	int firstIndex = 0;
	int visibleEntries = (MENU_END_ITERATION_VALUE - MENU_START_ITERATION_VALUE + 1);

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

	if (menuDataGlobal.currentItemIndex >= count)
	{
		menuDataGlobal.currentItemIndex = (count - 1U);
	}

	if (menuDataGlobal.currentItemIndex >= (uint8_t)(visibleEntries - 1))
	{
		firstIndex = (int)menuDataGlobal.currentItemIndex - (visibleEntries - 1);
	}

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = firstIndex + (i - MENU_START_ITERATION_VALUE);

		if (mNum >= count)
		{
			break;
		}

		smsSentMessage_t msg;

		if (smsGetSentMessage((uint8_t)mNum, &msg))
		{
			char preview[15];
			smsBuildMessagePreview(msg.text, preview, sizeof(preview), 14U);
			snprintf(line, sizeof(line), "%u %s", msg.destinationId, preview);
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
	char composeInfo[SCREEN_LINE_BUFFER_SIZE];
	char lineBuf[SMS_CHARS_PER_LINE + 1];
	int len = strlen(smsBuffer);
	int cursorLine, cursorCol, topLine;

	snprintf(composeInfo, sizeof(composeInfo), "%d/%d", len, SMS_MAX_LEN);

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
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START, composeInfo, FONT_SIZE_1);

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

static bool smsComposeGetPrivateContactAt(uint16_t listIndex, CodeplugContact_t *contact)
{
	if (contact == NULL)
	{
		return false;
	}

	return (codeplugContactGetDataForNumberInType((int)(listIndex + 1U), CONTACT_CALLTYPE_PC, contact) > 0);
}

static void smsComposeRenderContactSelect(void)
{
	char line[SCREEN_LINE_BUFFER_SIZE];
	int count = codeplugContactsGetCount(CONTACT_CALLTYPE_PC);

	displayClearBuf();
	menuDisplayTitle("Select contact");

	if (count <= 0)
	{
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_2_HEIGHT, "No private contacts", FONT_SIZE_2);
		displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT, "Red back", FONT_SIZE_1);
		displayThemeResetToDefault();
		displayRender();
		return;
	}

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(count, i);
		CodeplugContact_t contact;
		char contactName[17] = { 0 };

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (smsComposeGetPrivateContactAt((uint16_t)mNum, &contact))
		{
			codeplugUtilConvertBufToString(contact.name, contactName, 16);
			if (contactName[0] == 0)
			{
				strncpy(contactName, "(no name)", sizeof(contactName));
				contactName[sizeof(contactName) - 1] = 0;
			}
			snprintf(line, sizeof(line), "%s", contactName);
		}
		else
		{
			strncpy(line, "Invalid", sizeof(line));
			line[sizeof(line) - 1] = 0;
		}

		menuDisplayEntry(i, mNum, line, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT, "Green send  Red back", FONT_SIZE_1);
	displayThemeResetToDefault();
	displayRender();
}

static void smsComposeRenderDestinationSelect(void)
{
	const char *options[SMS_DESTINATION_OPTION_COUNT] = { "Select contact", "Manual ID" };

	menuDataGlobal.numItems = SMS_DESTINATION_OPTION_COUNT;
	if (smsComposeDestinationOptionIndex >= SMS_DESTINATION_OPTION_COUNT)
	{
		smsComposeDestinationOptionIndex = 0U;
	}
	menuDataGlobal.currentItemIndex = smsComposeDestinationOptionIndex;

	displayClearBuf();
	menuDisplayTitle("Send to");

	for (int i = MENU_START_ITERATION_VALUE; i < MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(SMS_DESTINATION_OPTION_COUNT, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		menuDisplayEntry(i, mNum, options[mNum], 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT, "Green select  Red back", FONT_SIZE_1);
	displayThemeResetToDefault();
	displayRender();
}

static void smsComposeRenderManualIdEntry(void)
{
	char line[SCREEN_LINE_BUFFER_SIZE];

	if (smsComposeManualIdBuffer[0] == 0)
	{
		strncpy(line, "ID:", sizeof(line));
		line[sizeof(line) - 1] = 0;
	}
	else
	{
		snprintf(line, sizeof(line), "ID:%s", smsComposeManualIdBuffer);
	}

	displayClearBuf();
	menuDisplayTitle("Manual ID");
	displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_START + FONT_SIZE_2_HEIGHT, line, FONT_SIZE_2);
	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintAt(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT, "Green send  Red back", FONT_SIZE_1);
	displayThemeResetToDefault();
	displayRender();
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

static bool smsSendBuffer(uint32_t destinationId)
{
	smsPackResult_t result;
	bool waitForAckEnabled = settingsIsOptionBitSet(BIT_SMS_ACK_WAIT);

	if (trxGetMode() != RADIO_MODE_DIGITAL)
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 2000, "DMR only", true);
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
	if (waitForAckEnabled)
	{
		smsRegisterOutgoingMessage(destinationId, trxDMRID, smsBuffer);
	}
	else
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
		smsComposeMode = SMS_COMPOSE_MODE_EDIT;
		smsComposeContactIndex = 0U;
		smsComposeDestinationOptionIndex = SMS_DESTINATION_OPTION_SELECT_CONTACT;
		smsComposeManualIdBuffer[0] = 0;
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
		if (smsComposeMode == SMS_COMPOSE_MODE_CONTACT_SELECT)
		{
			smsComposeRenderContactSelect();
		}
		else if (smsComposeMode == SMS_COMPOSE_MODE_DESTINATION_SELECT)
		{
			smsComposeRenderDestinationSelect();
		}
		else if (smsComposeMode == SMS_COMPOSE_MODE_MANUAL_ID)
		{
			smsComposeRenderManualIdEntry();
		}
		else
		{
			smsComposeRender(true, false);
		}
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (smsComposeMode == SMS_COMPOSE_MODE_MANUAL_ID)
		{
			smsComposeMode = SMS_COMPOSE_MODE_DESTINATION_SELECT;
			smsComposeRenderDestinationSelect();
			return MENU_STATUS_SUCCESS;
		}

		if (smsComposeMode == SMS_COMPOSE_MODE_DESTINATION_SELECT)
		{
			smsComposeMode = SMS_COMPOSE_MODE_EDIT;
			menuDataGlobal.currentItemIndex = 0;
			smsComposeRender(true, false);
			return MENU_STATUS_SUCCESS;
		}

		if (smsComposeMode == SMS_COMPOSE_MODE_CONTACT_SELECT)
		{
			smsComposeMode = SMS_COMPOSE_MODE_DESTINATION_SELECT;
			menuDataGlobal.currentItemIndex = 0;
			smsComposeRenderDestinationSelect();
			return MENU_STATUS_SUCCESS;
		}

		keypadAlphaEnable = false;
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (smsComposeMode == SMS_COMPOSE_MODE_DESTINATION_SELECT)
		{
			if (smsComposeDestinationOptionIndex == SMS_DESTINATION_OPTION_SELECT_CONTACT)
			{
				int privateContactsCount = codeplugContactsGetCount(CONTACT_CALLTYPE_PC);

				if (privateContactsCount <= 0)
				{
					soundSetMelody(MELODY_ERROR_BEEP);
					uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1800, "No private contacts", true);
				}
				else
				{
					smsComposeContactIndex = 0U;
					smsComposeMode = SMS_COMPOSE_MODE_CONTACT_SELECT;
					menuDataGlobal.currentItemIndex = 0;
					menuDataGlobal.numItems = privateContactsCount;
					smsComposeRenderContactSelect();
				}
			}
			else
			{
				smsComposeMode = SMS_COMPOSE_MODE_MANUAL_ID;
				smsComposeManualIdBuffer[0] = 0;
				smsComposeRenderManualIdEntry();
			}

			return MENU_STATUS_SUCCESS;
		}

		if (smsComposeMode == SMS_COMPOSE_MODE_CONTACT_SELECT)
		{
			CodeplugContact_t selectedContact;

			if (smsComposeGetPrivateContactAt(smsComposeContactIndex, &selectedContact) == false)
			{
				soundSetMelody(MELODY_ERROR_BEEP);
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1500, "Invalid contact", true);
				return MENU_STATUS_SUCCESS;
			}

			if (smsSendBuffer(selectedContact.tgNumber))
			{
				smsReplyDestinationEnabled = false;
				smsReplyDestinationId = 0U;
				smsComposeMode = SMS_COMPOSE_MODE_EDIT;
				keypadAlphaEnable = false;
				menuSystemPopAllAndDisplayRootMenu();
			}

			return MENU_STATUS_SUCCESS;
		}

		if (smsComposeMode == SMS_COMPOSE_MODE_MANUAL_ID)
		{
			char *endPtr = NULL;
			unsigned long parsedId = strtoul(smsComposeManualIdBuffer, &endPtr, 10);

			if ((smsComposeManualIdBuffer[0] == 0) || (endPtr == NULL) || (*endPtr != 0) || (parsedId == 0UL) || (parsedId > 0xFFFFFFFFUL))
			{
				soundSetMelody(MELODY_ERROR_BEEP);
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, 1500, "Invalid ID", true);
				return MENU_STATUS_SUCCESS;
			}

			if (smsSendBuffer((uint32_t)parsedId))
			{
				smsReplyDestinationEnabled = false;
				smsReplyDestinationId = 0U;
				smsComposeMode = SMS_COMPOSE_MODE_EDIT;
				keypadAlphaEnable = false;
				menuSystemPopAllAndDisplayRootMenu();
			}

			return MENU_STATUS_SUCCESS;
		}

		if (strlen(smsBuffer) == 0)
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
		else
		{
			if (smsReplyDestinationEnabled && (smsReplyDestinationId != 0U))
			{
				if (smsSendBuffer(smsReplyDestinationId))
				{
					smsReplyDestinationEnabled = false;
					smsReplyDestinationId = 0U;
					smsComposeMode = SMS_COMPOSE_MODE_EDIT;
					keypadAlphaEnable = false;
					menuSystemPopAllAndDisplayRootMenu();
				}
			}
			else
			{
				smsComposeDestinationOptionIndex = SMS_DESTINATION_OPTION_SELECT_CONTACT;
				smsComposeMode = SMS_COMPOSE_MODE_DESTINATION_SELECT;
				menuDataGlobal.currentItemIndex = 0;
				menuDataGlobal.numItems = SMS_DESTINATION_OPTION_COUNT;
				smsComposeRenderDestinationSelect();
			}
		}

		return MENU_STATUS_SUCCESS;
	}

	if (smsComposeMode == SMS_COMPOSE_MODE_CONTACT_SELECT)
	{
		int privateContactsCount = codeplugContactsGetCount(CONTACT_CALLTYPE_PC);

		if (privateContactsCount <= 0)
		{
			smsComposeRenderContactSelect();
			return MENU_STATUS_SUCCESS;
		}

		if (smsComposeContactIndex >= (uint16_t)privateContactsCount)
		{
			smsComposeContactIndex = (uint16_t)(privateContactsCount - 1);
			menuDataGlobal.currentItemIndex = (int)smsComposeContactIndex;
		}

		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
		{
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, privateContactsCount);
			smsComposeContactIndex = (uint16_t)menuDataGlobal.currentItemIndex;
			smsComposeRenderContactSelect();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, privateContactsCount);
			smsComposeContactIndex = (uint16_t)menuDataGlobal.currentItemIndex;
			smsComposeRenderContactSelect();
			return MENU_STATUS_SUCCESS;
		}

		return MENU_STATUS_SUCCESS;
	}

	if (smsComposeMode == SMS_COMPOSE_MODE_DESTINATION_SELECT)
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
		{
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, SMS_DESTINATION_OPTION_COUNT);
			smsComposeDestinationOptionIndex = (uint8_t)menuDataGlobal.currentItemIndex;
			smsComposeRenderDestinationSelect();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, SMS_DESTINATION_OPTION_COUNT);
			smsComposeDestinationOptionIndex = (uint8_t)menuDataGlobal.currentItemIndex;
			smsComposeRenderDestinationSelect();
			return MENU_STATUS_SUCCESS;
		}

		return MENU_STATUS_SUCCESS;
	}

	if (smsComposeMode == SMS_COMPOSE_MODE_MANUAL_ID)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT) || KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			size_t len = strlen(smsComposeManualIdBuffer);
			if (len > 0U)
			{
				smsComposeManualIdBuffer[len - 1U] = 0;
			}
			smsComposeRenderManualIdEntry();
			return MENU_STATUS_SUCCESS;
		}

		char digit = 0;
		if (KEYCHECK_SHORTUP(ev->keys, KEY_0)) digit = '0';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_1)) digit = '1';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_2)) digit = '2';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_3)) digit = '3';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_4)) digit = '4';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_5)) digit = '5';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_6)) digit = '6';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_7)) digit = '7';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_8)) digit = '8';
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_9)) digit = '9';

		if (digit != 0)
		{
			size_t len = strlen(smsComposeManualIdBuffer);
			if (len < (sizeof(smsComposeManualIdBuffer) - 1U))
			{
				smsComposeManualIdBuffer[len] = digit;
				smsComposeManualIdBuffer[len + 1U] = 0;
				smsComposeRenderManualIdEntry();
			}
			else
			{
				soundSetMelody(MELODY_ERROR_BEEP);
			}
			return MENU_STATUS_SUCCESS;
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
		if (menuDataGlobal.currentItemIndex < (count - 1U))
		{
			menuDataGlobal.currentItemIndex++;
		}
		smsInboxRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		if (menuDataGlobal.currentItemIndex > 0U)
		{
			menuDataGlobal.currentItemIndex--;
		}
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
		if (menuDataGlobal.currentItemIndex < (count - 1U))
		{
			menuDataGlobal.currentItemIndex++;
		}
		smsSentRender();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		if (menuDataGlobal.currentItemIndex > 0U)
		{
			menuDataGlobal.currentItemIndex--;
		}
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

	if ((KEYCHECK_SHORTUP(ev->keys, KEY_0) || KEYCHECK_PRESS(ev->keys, KEY_0)) &&
		(KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_0) == false))
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
		smsRxRespondMode = false;
		smsRxRespondOptionIndex = 0U;
		(void)smsConsumeRxNotification();

		if (smsLoadPopupMessage() == false)
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		smsViewSource = SMS_VIEW_SOURCE_RX_POPUP;
		smsViewInboxMessage = smsPopupMessage;
		smsViewMessageIndex = smsPopupMessageIndex;
		smsViewTopLine = 0U;
		snprintf(smsViewPeerText, sizeof(smsViewPeerText), "From: %s", smsPopupSource);

		menuDataGlobal.currentItemIndex = 0;
		menuDataGlobal.numItems = SMS_RX_POPUP_ITEMS_COUNT;
		smsRxPopupRender();
		return menuStatus;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		if (smsRxRespondMode)
		{
			smsRxRespondRender();
		}
		else
		{
			smsRxPopupRender();
		}
		return menuStatus;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (smsRxRespondMode)
		{
			smsRxRespondMode = false;
			smsRxPopupRender();
			return MENU_STATUS_SUCCESS;
		}

		smsReplyDestinationEnabled = false;
		smsReplyDestinationId = 0U;
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		if (smsRxRespondMode)
		{
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, SMS_RESPOND_OPTION_COUNT);
			smsRxRespondOptionIndex = (uint8_t)menuDataGlobal.currentItemIndex;
			smsRxRespondRender();
		}
		else
		{
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, SMS_RX_POPUP_ITEMS_COUNT);
			smsRxPopupRender();
		}
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		if (smsRxRespondMode)
		{
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, SMS_RESPOND_OPTION_COUNT);
			smsRxRespondOptionIndex = (uint8_t)menuDataGlobal.currentItemIndex;
			smsRxRespondRender();
		}
		else
		{
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, SMS_RX_POPUP_ITEMS_COUNT);
			smsRxPopupRender();
		}
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (smsRxRespondMode)
		{
			if (smsRxRespondOptionIndex == SMS_RESPOND_OPTION_TO_SENDER)
			{
				smsReplyDestinationEnabled = true;
				smsReplyDestinationId = smsPopupMessage.sourceId;
			}
			else
			{
				smsReplyDestinationEnabled = false;
				smsReplyDestinationId = 0U;
			}

			smsRxRespondMode = false;
			menuSystemPushNewMenu(MENU_SMS_COMPOSE);
			return MENU_STATUS_SUCCESS;
		}

		switch (menuDataGlobal.currentItemIndex)
		{
			case SMS_RX_POPUP_ITEM_VIEW:
					smsViewSource = SMS_VIEW_SOURCE_RX_POPUP;
					smsViewInboxMessage = smsPopupMessage;
					smsViewMessageIndex = smsPopupMessageIndex;
					smsViewTopLine = 0U;
					snprintf(smsViewPeerText, sizeof(smsViewPeerText), "From: %s", smsPopupSource);
				menuSystemPushNewMenu(MENU_SMS_VIEW);
				break;

			case SMS_RX_POPUP_ITEM_VIEW_LATER:
				smsReplyDestinationEnabled = false;
				smsReplyDestinationId = 0U;
				menuSystemPopPreviousMenu();
				break;

			case SMS_RX_POPUP_ITEM_RESPOND:
				smsRxRespondMode = true;
				smsRxRespondOptionIndex = SMS_RESPOND_OPTION_SELECT_CONTACT;
				smsRxRespondRender();
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
	const char *messageText;
	char displayText[SMS_MAX_LEN + 1U];
	uint16_t messageLength;
	uint16_t lineStarts[SMS_VIEW_MAX_LINES];
	uint8_t lineLengths[SMS_VIEW_MAX_LINES];
	int totalLines;
	int visibleLines;
	int maxTopLine;

	if (smsViewSource == SMS_VIEW_SOURCE_SENT)
	{
		smsSentMessage_t message;

		if (smsGetSentMessage(smsViewMessageIndex, &message))
		{
			messageText = message.text;
		}
		else
		{
			messageText = "";
		}
	}
	else
	{
		messageText = smsViewInboxMessage.text;
	}

	messageLength = smsBuildViewText(messageText, displayText, sizeof(displayText));
	totalLines = smsBuildViewLineLayout(displayText, messageLength, lineStarts, lineLengths, SMS_VIEW_MAX_LINES);
	visibleLines = smsViewGetVisibleLines();
	maxTopLine = (totalLines > visibleLines) ? (totalLines - visibleLines) : 0;

	if (isFirstRun)
	{
		smsViewTopLine = 0U;
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

	if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) || KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
	{
		if ((int)smsViewTopLine < maxTopLine)
		{
			smsViewTopLine++;
			smsViewRender();
		}
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		if (smsViewTopLine > 0U)
		{
			smsViewTopLine--;
			smsViewRender();
		}
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

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuSystemPopAllAndDisplayRootMenu();
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
