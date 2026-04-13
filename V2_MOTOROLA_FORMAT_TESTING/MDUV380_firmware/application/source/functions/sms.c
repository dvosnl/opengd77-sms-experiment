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
#include <stddef.h>

#include "functions/sms.h"
#include "functions/settings.h"
#include "functions/ticks.h"
#include "functions/trx.h"
#include "hardware/HR-C6000.h"
#include "hardware/EEPROM.h"
#include "usb/usb_com.h"

#define SMS_STORAGE_ADDRESS                    0x0F0000
#define SMS_STORAGE_MAGIC                      0x534D5349U
#define SMS_STORAGE_VERSION                    5U
#define SMS_STORAGE_DEBOUNCE_MS                1500U
#define SMS_TX_ACK_TIMEOUT_MS                  6000U
#define SMS_MOTOROLA_UDP_PORT                0x0FA7U
#define SMS_MOTOROLA_IPV4_PROTOCOL           0x11U
#define SMS_MOTOROLA_IPV4_TTL                0x01U
#define SMS_MOTOROLA_TEXT_OFFSET               38U
#define SMS_MOTOROLA_INTERNAL_HEADER_SIZE      10U
#define SMS_STANDARD_TEXT_OFFSET               32U
#define SMS_STANDARD_UDP_PORT                0x1398U
	#define SMS_RX_TESTING_OPEN_DOOR                0U
#define SMS_RX_TESTING_RAW_SERIAL_LOG           0U
#define SMS_RX_TESTING_DISABLE_ACK_TX           1U
#define SMS_RX_TESTING_ACK_MINIMAL              0U
#define SMS_RX_ASSEMBLY_TIMEOUT_MS           5000U
#define SMS_RX_RETX_GUARD_MS                  250U
#define SMS_RX_ASSEMBLY_FLAG_ACTIVE            0x01U
#define SMS_RX_ASSEMBLY_FLAG_RESPONSE_REQ      0x02U
#define SMS_RX_ASSEMBLY_FLAG_FMT_03            0x04U
#define SMS_RX_ASSEMBLY_FLAG_BLOCK_PREFIX2     0x08U

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t inboxCount;
	uint32_t sentCount;
	uint32_t quickTextCount;
	uint32_t checksum;
	smsInboxMessage_t inboxMessages[SMS_INBOX_MAX_MESSAGES];
	smsSentMessage_t sentMessages[SMS_SENT_MAX_MESSAGES];
	smsQuickTextMessage_t quickTextMessages[SMS_QUICKTEXT_MAX_MESSAGES];
} smsStorage_t;

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t messageCount;
	uint32_t checksum;
	smsInboxMessage_t messages[SMS_INBOX_MAX_MESSAGES];
} smsLegacyInboxStorage_t;

static smsPreparedMessage_t queuedMessage;
static bool queuedMessageValid = false;

typedef struct
{
	uint8_t expectedBlocks;
	uint8_t receivedBlocks;
	uint8_t padOctets;
	uint8_t flags;
	uint32_t sourceId;
	uint8_t payload[SMS_MAX_RX_DATA_BLOCKS * SMS_BLOCK_DATA_BYTES];
} smsRxAssembly_t;

typedef struct
{
	bool active;
	uint32_t destinationId;
} smsAckResponseTracking_t;

typedef struct
{
	bool active;
	uint32_t destinationId;
	uint32_t sourceId;
	ticksTimer_t ackTimer;
	char text[SMS_MAX_TX_TEXT_LENGTH + 1U];
} smsOutgoingTracking_t;

static smsInboxMessage_t inboxMessages[SMS_INBOX_MAX_MESSAGES];
static uint8_t inboxStart = 0U;
static uint8_t inboxCount = 0U;
static uint16_t smsIpSequenceNumber = 0U;
static smsSentMessage_t sentMessages[SMS_SENT_MAX_MESSAGES];
static uint8_t sentStart = 0U;
static uint8_t sentCount = 0U;
static uint8_t quickTextCount = 0U;
static bool inboxUnreadNotification = false;
static smsRxAssembly_t rxAssembly = { 0 };
static smsAckResponseTracking_t ackResponseTracking = { 0 };
static smsOutgoingTracking_t outgoingTracking = { 0 };
static smsTxEvent_t pendingTxEvent = SMS_TX_EVENT_NONE;
static volatile bool smsStorageDirty = false;
static uint32_t smsStorageDirtySinceTick = 0U;
static uint32_t smsRxTestingLastStoreTick = 0U;

static void smsResetRxAssembly(void);
static void smsScheduleAckResponse(uint32_t destinationId);
static bool smsQueueAckResponseMessage(uint32_t destinationId, uint32_t sourceId);
static uint16_t smsCrc16Ccitt(const uint8_t *data, uint8_t length);
static void smsResetOutgoingTracking(void);
static void smsSetPendingTxEvent(smsTxEvent_t event);
static smsPackResult_t smsConvertTextToUtf16LeUpper(const char *text, uint8_t *payload, uint16_t *payloadLength);
static smsPackResult_t smsBuildMotorolaPayload(uint32_t destinationId, uint32_t sourceId, const char *text, uint8_t *payload, uint16_t *payloadLength, uint8_t *padOctetCount);
static bool smsDecodeMotorolaPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, char *textOut);
static bool smsDecodeStandardPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, char *textOut);
static bool smsDecodeIpUdpPayloadWithoutCrc(const uint8_t *payload, uint16_t payloadLength, char *textOut);
static bool smsTryInferConfirmedBlockLayout(const uint8_t *frame, uint8_t *payloadBytesOut, uint16_t *ipPacketLengthOut);
static uint8_t smsComputeRequiredBlocks(uint16_t payloadLength, uint8_t bytesPerBlock);
static bool smsDecodeUtf16PayloadSliding(const uint8_t *payload, uint16_t payloadLength, char *textOut);
static bool smsDecodeAsciiPayloadSliding(const uint8_t *payload, uint16_t payloadLength, char *textOut);
static bool smsForceDecodePayload(const uint8_t *payload, uint16_t payloadLength, char *textOut);
static void smsStoreInboxMessage(uint32_t sourceId, const char *text);
static void smsTestingStoreOpenDoorMarker(uint32_t sourceId);
static uint32_t smsStorageChecksum(const smsStorage_t *storage);
static uint32_t smsLegacyInboxStorageChecksum(const smsLegacyInboxStorage_t *storage);
static void smsStorageMarkDirty(void);
static void smsStorageBuildSnapshot(smsStorage_t *storage);
static bool smsStoragePersist(void);
static void smsStorageLoad(void);

static inline bool smsRxAssemblyIsActive(void)
{
	return ((rxAssembly.flags & SMS_RX_ASSEMBLY_FLAG_ACTIVE) != 0U);
}

static inline void smsRxAssemblySetActive(bool active)
{
	if (active)
	{
		rxAssembly.flags |= SMS_RX_ASSEMBLY_FLAG_ACTIVE;
	}
	else
	{
		rxAssembly.flags &= (uint8_t)(~SMS_RX_ASSEMBLY_FLAG_ACTIVE);
	}
}

static inline bool smsRxAssemblyIsResponseRequested(void)
{
	return ((rxAssembly.flags & SMS_RX_ASSEMBLY_FLAG_RESPONSE_REQ) != 0U);
}

static inline void smsRxAssemblySetResponseRequested(bool responseRequested)
{
	if (responseRequested)
	{
		rxAssembly.flags |= SMS_RX_ASSEMBLY_FLAG_RESPONSE_REQ;
	}
	else
	{
		rxAssembly.flags &= (uint8_t)(~SMS_RX_ASSEMBLY_FLAG_RESPONSE_REQ);
	}
}

static inline bool smsRxAssemblyIsFormat03(void)
{
	return ((rxAssembly.flags & SMS_RX_ASSEMBLY_FLAG_FMT_03) != 0U);
}

static inline void smsRxAssemblySetFormat03(bool isFormat03)
{
	if (isFormat03)
	{
		rxAssembly.flags |= SMS_RX_ASSEMBLY_FLAG_FMT_03;
	}
	else
	{
		rxAssembly.flags &= (uint8_t)(~SMS_RX_ASSEMBLY_FLAG_FMT_03);
	}
}

static inline uint8_t smsRxAssemblyGetBlockPayloadBytes(void)
{
	return ((rxAssembly.flags & SMS_RX_ASSEMBLY_FLAG_BLOCK_PREFIX2) != 0U) ? (uint8_t)(SMS_BLOCK_DATA_BYTES - 2U) : SMS_BLOCK_DATA_BYTES;
}

static inline void smsRxAssemblySetBlockPayloadBytes(uint8_t payloadBytes)
{
	if (payloadBytes == (uint8_t)(SMS_BLOCK_DATA_BYTES - 2U))
	{
		rxAssembly.flags |= SMS_RX_ASSEMBLY_FLAG_BLOCK_PREFIX2;
	}
	else
	{
		rxAssembly.flags &= (uint8_t)(~SMS_RX_ASSEMBLY_FLAG_BLOCK_PREFIX2);
	}
}

void smsInit(void)
{
	memset(&queuedMessage, 0, sizeof(queuedMessage));
	queuedMessageValid = false;
	inboxStart = 0U;
	inboxCount = 0U;
	sentStart = 0U;
	sentCount = 0U;
	quickTextCount = 0U;
	inboxUnreadNotification = false;
	memset(inboxMessages, 0, sizeof(inboxMessages));
	memset(sentMessages, 0, sizeof(sentMessages));
	smsResetRxAssembly();
	smsResetOutgoingTracking();
	pendingTxEvent = SMS_TX_EVENT_NONE;
	smsStorageDirty = false;
	smsStorageDirtySinceTick = 0U;
	smsStorageLoad();

#if SMS_RX_TESTING_OPEN_DOOR
	{
		bool bootMarkerPresent = false;

		for (uint8_t i = 0U; i < inboxCount; i++)
		{
			uint8_t index = (uint8_t)((inboxStart + i) % SMS_INBOX_MAX_MESSAGES);

			if ((inboxMessages[index].sourceId == 999000U) &&
				(strncmp(inboxMessages[index].text, "RX_BOOT_INBOX_OK", SMS_MAX_RX_TEXT_LENGTH) == 0))
			{
				bootMarkerPresent = true;
				break;
			}
		}

		if (!bootMarkerPresent)
		{
			smsStoreInboxMessage(999000U, "RX_BOOT_INBOX_OK");
		}
	}
#endif
}

static void smsResetOutgoingTracking(void)
{
	memset(&outgoingTracking, 0, sizeof(outgoingTracking));
}

static void smsSetPendingTxEvent(smsTxEvent_t event)
{
	pendingTxEvent = event;
}

void smsDebugLogRawIncomingFrame(uint8_t dataType, const uint8_t *frame)
{
#if SMS_RX_TESTING_RAW_SERIAL_LOG
	static const char hexChars[] = "0123456789ABCDEF";
	char line[64];
	uint8_t pos = 0U;

	if (frame == NULL)
	{
		return;
	}

	line[pos++] = 'R';
	line[pos++] = 'X';
	line[pos++] = ' ';
	line[pos++] = 'T';
	line[pos++] = '=';
	line[pos++] = hexChars[(dataType >> 4) & 0x0FU];
	line[pos++] = hexChars[dataType & 0x0FU];
	line[pos++] = ' ';

	for (uint8_t i = 0U; i < SMS_BLOCK_DATA_BYTES; i++)
	{
		uint8_t value = frame[i];

		line[pos++] = hexChars[(value >> 4) & 0x0FU];
		line[pos++] = hexChars[value & 0x0FU];
		if (i < (SMS_BLOCK_DATA_BYTES - 1U))
		{
			line[pos++] = ' ';
		}
	}

	line[pos++] = '\r';
	line[pos++] = '\n';
	line[pos] = 0;

	USB_DEBUG_PRINT(line);
#else
	(void)dataType;
	(void)frame;
#endif
}

static uint32_t smsStorageChecksum(const smsStorage_t *storage)
{
	const uint8_t *bytes = (const uint8_t *)storage;
	const uint32_t checksumOffset = (uint32_t)offsetof(smsStorage_t, checksum);
	uint32_t checksum = 2166136261UL;

	for (uint32_t i = 0U; i < sizeof(smsStorage_t); i++)
	{
		if ((i >= checksumOffset) && (i < (checksumOffset + sizeof(storage->checksum))))
		{
			continue;
		}

		checksum ^= bytes[i];
		checksum *= 16777619UL;
	}

	return checksum;
}

static uint32_t smsLegacyInboxStorageChecksum(const smsLegacyInboxStorage_t *storage)
{
	const uint8_t *bytes = (const uint8_t *)storage;
	const uint32_t checksumOffset = (uint32_t)offsetof(smsLegacyInboxStorage_t, checksum);
	uint32_t checksum = 2166136261UL;

	for (uint32_t i = 0U; i < sizeof(smsLegacyInboxStorage_t); i++)
	{
		if ((i >= checksumOffset) && (i < (checksumOffset + sizeof(storage->checksum))))
		{
			continue;
		}

		checksum ^= bytes[i];
		checksum *= 16777619UL;
	}

	return checksum;
}

static void smsStorageMarkDirty(void)
{
	smsStorageDirty = true;
}

static void smsStorageBuildSnapshot(smsStorage_t *storage)
{
	smsStorage_t existingStorage;
	bool existingStorageValid = false;

	if (storage == NULL)
	{
		return;
	}

	memset(storage, 0, sizeof(*storage));
	storage->magic = SMS_STORAGE_MAGIC;
	storage->version = SMS_STORAGE_VERSION;
	storage->inboxCount = inboxCount;
	storage->sentCount = sentCount;
	storage->quickTextCount = quickTextCount;

	for (uint8_t i = 0U; i < inboxCount; i++)
	{
		smsInboxMessage_t message;

		if (smsGetInboxMessage(i, &message))
		{
			storage->inboxMessages[i] = message;
			storage->inboxMessages[i].text[SMS_MAX_RX_TEXT_LENGTH] = 0;
		}
	}

	for (uint8_t i = 0U; i < sentCount; i++)
	{
		smsSentMessage_t message;

		if (smsGetSentMessage(i, &message))
		{
			storage->sentMessages[i] = message;
			storage->sentMessages[i].text[SMS_MAX_TX_TEXT_LENGTH] = 0;
		}
	}

	if (EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)&existingStorage, (int)sizeof(existingStorage)) &&
		(existingStorage.magic == SMS_STORAGE_MAGIC) &&
		(existingStorage.version == SMS_STORAGE_VERSION) &&
		(existingStorage.quickTextCount <= SMS_QUICKTEXT_MAX_MESSAGES) &&
		(existingStorage.checksum == smsStorageChecksum(&existingStorage)))
	{
		existingStorageValid = true;
	}

	if (existingStorageValid)
	{
		storage->quickTextCount = existingStorage.quickTextCount;
		for (uint8_t i = 0U; i < storage->quickTextCount; i++)
		{
			storage->quickTextMessages[i] = existingStorage.quickTextMessages[i];
			storage->quickTextMessages[i].title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
			storage->quickTextMessages[i].text[SMS_MAX_TX_TEXT_LENGTH] = 0;
		}
	}
	else
	{
		storage->quickTextCount = 0U;
		memset(storage->quickTextMessages, 0, sizeof(storage->quickTextMessages));
	}

	storage->checksum = smsStorageChecksum(storage);
}

static bool smsStoragePersist(void)
{
	smsStorage_t storage;

	smsStorageBuildSnapshot(&storage);
	return EEPROM_Write(SMS_STORAGE_ADDRESS, (uint8_t *)&storage, (int)sizeof(storage));
}

static void smsStorageLoad(void)
{
	smsStorage_t storage;
	smsLegacyInboxStorage_t legacyStorage;

	if (EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)&storage, (int)sizeof(storage)) &&
		(storage.magic == SMS_STORAGE_MAGIC) &&
		(storage.version == SMS_STORAGE_VERSION) &&
		(storage.inboxCount <= SMS_INBOX_MAX_MESSAGES) &&
		(storage.sentCount <= SMS_SENT_MAX_MESSAGES) &&
		(storage.quickTextCount <= SMS_QUICKTEXT_MAX_MESSAGES) &&
		(storage.checksum == smsStorageChecksum(&storage)))
	{
		inboxStart = 0U;
		inboxCount = (uint8_t)storage.inboxCount;
		sentStart = 0U;
		sentCount = (uint8_t)storage.sentCount;
		quickTextCount = (uint8_t)storage.quickTextCount;
		inboxUnreadNotification = false;
		memset(inboxMessages, 0, sizeof(inboxMessages));
		memset(sentMessages, 0, sizeof(sentMessages));

		for (uint8_t i = 0U; i < inboxCount; i++)
		{
			inboxMessages[i] = storage.inboxMessages[i];
			inboxMessages[i].text[SMS_MAX_RX_TEXT_LENGTH] = 0;
		}

		for (uint8_t i = 0U; i < sentCount; i++)
		{
			sentMessages[i] = storage.sentMessages[i];
			sentMessages[i].text[SMS_MAX_TX_TEXT_LENGTH] = 0;
		}

		return;
	}

	if (!EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)&legacyStorage, (int)sizeof(legacyStorage)))
	{
		return;
	}

	if ((legacyStorage.magic != SMS_STORAGE_MAGIC) || (legacyStorage.version != 1U) ||
		(legacyStorage.messageCount > SMS_INBOX_MAX_MESSAGES) ||
		(legacyStorage.checksum != smsLegacyInboxStorageChecksum(&legacyStorage)))
	{
		return;
	}

	inboxStart = 0U;
	inboxCount = (uint8_t)legacyStorage.messageCount;
	sentStart = 0U;
	sentCount = 0U;
	quickTextCount = 0U;
	inboxUnreadNotification = false;
	memset(inboxMessages, 0, sizeof(inboxMessages));
	memset(sentMessages, 0, sizeof(sentMessages));

	for (uint8_t i = 0U; i < inboxCount; i++)
	{
		inboxMessages[i] = legacyStorage.messages[i];
		inboxMessages[i].text[SMS_MAX_RX_TEXT_LENGTH] = 0;
	}
}

static bool smsIsPrintableCharacter(uint8_t c)
{
	return (((c >= 0x20U) && (c <= 0x7EU)) || (c == '\r') || (c == '\n'));
}

static bool smsMapUnicodeToDisplay(uint8_t high, uint8_t low, uint8_t *mapped, bool *utf16Like)
{
	uint16_t codepoint;

	if ((mapped == NULL) || (utf16Like == NULL))
	{
		return false;
	}

	*utf16Like = false;

	if (high == 0x00U)
	{
		if (smsIsPrintableCharacter(low))
		{
			*mapped = low;
			*utf16Like = true;
			return true;
		}

		if ((low == '\t') || (low == 0xA0U))
		{
			*mapped = ' ';
			*utf16Like = true;
			return true;
		}
	}

	if (low == 0x00U)
	{
		if (smsIsPrintableCharacter(high))
		{
			*mapped = high;
			*utf16Like = true;
			return true;
		}

		if ((high == '\t') || (high == 0xA0U))
		{
			*mapped = ' ';
			*utf16Like = true;
			return true;
		}
	}

	codepoint = (uint16_t)(((uint16_t)high << 8) | low);

	// Common punctuation from smart phones / Motorola CPS templates.
	switch (codepoint)
	{
		case 0x2018U:
		case 0x2019U:
			*mapped = '\'';
			*utf16Like = true;
			return true;

		case 0x201CU:
		case 0x201DU:
			*mapped = '"';
			*utf16Like = true;
			return true;

		case 0x2013U:
		case 0x2014U:
			*mapped = '-';
			*utf16Like = true;
			return true;

		case 0x2026U:
			*mapped = '.';
			*utf16Like = true;
			return true;

		case 0x00A0U:
			*mapped = ' ';
			*utf16Like = true;
			return true;

		default:
			break;
	}

	return false;
}

static void smsStoreInboxMessage(uint32_t sourceId, const char *text)
{
	uint8_t writeIndex;

	if ((text == NULL) || (text[0] == 0))
	{
		return;
	}

	if (inboxCount < SMS_INBOX_MAX_MESSAGES)
	{
		writeIndex = (uint8_t)((inboxStart + inboxCount) % SMS_INBOX_MAX_MESSAGES);
		inboxCount++;
	}
	else
	{
		writeIndex = inboxStart;
		inboxStart = (uint8_t)((inboxStart + 1U) % SMS_INBOX_MAX_MESSAGES);
	}

	inboxMessages[writeIndex].sourceId = sourceId;
	strncpy(inboxMessages[writeIndex].text, text, SMS_MAX_RX_TEXT_LENGTH);
	inboxMessages[writeIndex].text[SMS_MAX_RX_TEXT_LENGTH] = 0;
	inboxUnreadNotification = true;
	smsStorageMarkDirty();
}

static void smsTestingStoreOpenDoorMarker(uint32_t sourceId)
{
#if SMS_RX_TESTING_OPEN_DOOR
	if ((sourceId == 0U) || (sourceId == trxDMRID))
	{
		return;
	}

	smsStoreInboxMessage(sourceId, "RX_OPEN_DOOR_H06");
#else
	(void)sourceId;
#endif
}

static void smsStoreSentMessageInternal(uint32_t destinationId, const char *text)
{
	uint8_t writeIndex;

	if ((text == NULL) || (text[0] == 0) || (destinationId == 0U) || (destinationId > 0x00FFFFFFU))
	{
		return;
	}

	if (sentCount < SMS_SENT_MAX_MESSAGES)
	{
		writeIndex = (uint8_t)((sentStart + sentCount) % SMS_SENT_MAX_MESSAGES);
		sentCount++;
	}
	else
	{
		writeIndex = sentStart;
		sentStart = (uint8_t)((sentStart + 1U) % SMS_SENT_MAX_MESSAGES);
	}

	sentMessages[writeIndex].destinationId = destinationId;
	strncpy(sentMessages[writeIndex].text, text, SMS_MAX_TX_TEXT_LENGTH);
	sentMessages[writeIndex].text[SMS_MAX_TX_TEXT_LENGTH] = 0;
	smsStorageMarkDirty();
}

static bool smsDecodeUtf16Payload(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	uint16_t inIndex = 0U;
	uint16_t outIndex = 0U;
	uint16_t utf16PatternCount = 0U;
	uint16_t replacedCount = 0U;

	if ((payload == NULL) || (textOut == NULL) || ((payloadLength & 0x01U) != 0U))
	{
		return false;
	}

	while ((inIndex + 1U) < payloadLength)
	{
		uint8_t high = payload[inIndex++];
		uint8_t low = payload[inIndex++];
		uint8_t c = '?';
		bool utf16Like = false;

		if ((high == 0x00U) && (low == 0x00U))
		{
			break;
		}

		if (smsMapUnicodeToDisplay(high, low, &c, &utf16Like))
		{
			if (utf16Like)
			{
				utf16PatternCount++;
			}
		}
		else
		{
			replacedCount++;
		}

		if (outIndex >= SMS_MAX_RX_TEXT_LENGTH)
		{
			break;
		}

		textOut[outIndex++] = (char)c;
	}

	textOut[outIndex] = 0;

	if ((outIndex == 0U) || (utf16PatternCount == 0U))
	{
		return false;
	}

	if ((outIndex <= 2U) && (replacedCount != 0U))
	{
		return false;
	}

	// Reject random binary that accidentally contains a single UTF-16-like pair.
	if ((replacedCount * 3U) > outIndex)
	{
		return false;
	}

	return true;
}

static bool smsDecodeAsciiPayload(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	uint16_t outIndex = 0U;
	uint16_t printableCount = 0U;
	uint16_t replacedCount = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength == 0U))
	{
		return false;
	}

	for (uint16_t i = 0U; (i < payloadLength) && (outIndex < SMS_MAX_RX_TEXT_LENGTH); i++)
	{
		uint8_t c = payload[i];

		if (c == 0x00U)
		{
			break;
		}

		if (smsIsPrintableCharacter(c))
		{
			textOut[outIndex++] = (char)c;
			printableCount++;
		}
		else if ((c == '\r') || (c == '\n') || (c == '\t'))
		{
			textOut[outIndex++] = ' ';
			printableCount++;
		}
		else
		{
			textOut[outIndex++] = '?';
			replacedCount++;
		}
	}

	textOut[outIndex] = 0;

	if ((outIndex == 0U) || (printableCount == 0U))
	{
		return false;
	}

	// Accept ASCII only when mostly printable; avoids storing IP/UDP header bytes as text.
	return ((replacedCount * 4U) <= outIndex);
}

static uint16_t smsTextLengthBounded(const char *text)
{
	uint16_t length = 0U;

	if (text == NULL)
	{
		return 0U;
	}

	while ((length < SMS_MAX_RX_TEXT_LENGTH) && (text[length] != 0))
	{
		length++;
	}

	return length;
}

static bool smsDecodeUtf16PayloadSliding(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	char candidate[SMS_MAX_RX_TEXT_LENGTH + 1U];
	char best[SMS_MAX_RX_TEXT_LENGTH + 1U];
	uint16_t bestLength = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength < 4U))
	{
		return false;
	}

	best[0] = 0;

	for (uint16_t offset = 0U; (offset + 4U) <= payloadLength; offset++)
	{
		uint16_t remaining = (uint16_t)(payloadLength - offset);
		uint16_t evenLength = (uint16_t)(remaining & 0xFFFEU);

		if (evenLength < 4U)
		{
			continue;
		}

		if (smsDecodeUtf16Payload(&payload[offset], evenLength, candidate))
		{
			uint16_t candidateLength = smsTextLengthBounded(candidate);
			if (candidateLength > bestLength)
			{
				bestLength = candidateLength;
				strncpy(best, candidate, SMS_MAX_RX_TEXT_LENGTH);
				best[SMS_MAX_RX_TEXT_LENGTH] = 0;
			}
		}
	}

	if (bestLength < 3U)
	{
		return false;
	}

	strncpy(textOut, best, SMS_MAX_RX_TEXT_LENGTH);
	textOut[SMS_MAX_RX_TEXT_LENGTH] = 0;
	return true;
}

static bool smsDecodeAsciiPayloadSliding(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	char candidate[SMS_MAX_RX_TEXT_LENGTH + 1U];
	char best[SMS_MAX_RX_TEXT_LENGTH + 1U];
	uint16_t bestLength = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength < 3U))
	{
		return false;
	}

	best[0] = 0;

	for (uint16_t offset = 0U; (offset + 3U) <= payloadLength; offset++)
	{
		uint16_t remaining = (uint16_t)(payloadLength - offset);

		if (smsDecodeAsciiPayload(&payload[offset], remaining, candidate))
		{
			uint16_t candidateLength = smsTextLengthBounded(candidate);
			if (candidateLength > bestLength)
			{
				bestLength = candidateLength;
				strncpy(best, candidate, SMS_MAX_RX_TEXT_LENGTH);
				best[SMS_MAX_RX_TEXT_LENGTH] = 0;
			}
		}
	}

	if (bestLength < 3U)
	{
		return false;
	}

	strncpy(textOut, best, SMS_MAX_RX_TEXT_LENGTH);
	textOut[SMS_MAX_RX_TEXT_LENGTH] = 0;
	return true;
}

static bool smsForceDecodePayload(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	uint16_t outIndex = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength == 0U))
	{
		return false;
	}

	if (smsDecodeUtf16PayloadSliding(payload, payloadLength, textOut))
	{
		return true;
	}

	if (smsDecodeAsciiPayloadSliding(payload, payloadLength, textOut))
	{
		return true;
	}

	for (uint16_t i = 0U; (i < payloadLength) && (outIndex < SMS_MAX_RX_TEXT_LENGTH); i++)
	{
		uint8_t c = payload[i];
		if ((c >= 0x20U) && (c <= 0x7EU))
		{
			textOut[outIndex++] = (char)c;
		}
	}

	if (outIndex >= 3U)
	{
		textOut[outIndex] = 0;
		return true;
	}

	strncpy(textOut, "RAW_SMS_PAYLOAD", SMS_MAX_RX_TEXT_LENGTH);
	textOut[SMS_MAX_RX_TEXT_LENGTH] = 0;
	return true;
}

static uint8_t smsComputeRequiredBlocks(uint16_t payloadLength, uint8_t bytesPerBlock)
{
	uint16_t requiredBlocks;

	if ((payloadLength == 0U) || (bytesPerBlock == 0U))
	{
		return 0U;
	}

	requiredBlocks = (uint16_t)((payloadLength + bytesPerBlock - 1U) / bytesPerBlock);
	if (requiredBlocks > SMS_MAX_RX_DATA_BLOCKS)
	{
		return 0U;
	}

	return (uint8_t)requiredBlocks;
}

static bool smsTryInferConfirmedBlockLayout(const uint8_t *frame, uint8_t *payloadBytesOut, uint16_t *ipPacketLengthOut)
{
	uint16_t ipPacketLength;

	if ((frame == NULL) || (payloadBytesOut == NULL) || (ipPacketLengthOut == NULL))
	{
		return false;
	}

	if ((frame[0] == 0x45U) && (frame[1] == 0x00U) && (frame[9] == SMS_MOTOROLA_IPV4_PROTOCOL))
	{
		ipPacketLength = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
		if ((ipPacketLength >= 20U) && (ipPacketLength <= SMS_MAX_RX_TRANSPORT_BYTES))
		{
			*payloadBytesOut = SMS_BLOCK_DATA_BYTES;
			*ipPacketLengthOut = ipPacketLength;
			return true;
		}
	}

	if ((frame[2] == 0x45U) && (frame[3] == 0x00U) && (frame[11] == SMS_MOTOROLA_IPV4_PROTOCOL))
	{
		ipPacketLength = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
		if ((ipPacketLength >= 20U) && (ipPacketLength <= SMS_MAX_RX_TRANSPORT_BYTES))
		{
			*payloadBytesOut = (SMS_BLOCK_DATA_BYTES - 2U);
			*ipPacketLengthOut = ipPacketLength;
			return true;
		}
	}

	return false;
}

static bool smsDecodeIpUdpPayloadWithoutCrc(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	uint8_t ipHeaderLength;
	uint16_t ipPacketLength;
	uint16_t udpOffset;
	uint16_t udpLength;
	uint16_t udpPayloadOffset;
	uint16_t udpPayloadLength;
	uint16_t srcPort;
	uint16_t dstPort;
	bool hasMotorolaPort;
	bool hasStandardPort;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength < (20U + 8U)))
	{
		return false;
	}

	if (((payload[0] & 0xF0U) != 0x40U) || (payload[9] != SMS_MOTOROLA_IPV4_PROTOCOL))
	{
		return false;
	}

	ipHeaderLength = (uint8_t)((payload[0] & 0x0FU) * 4U);
	if ((ipHeaderLength < 20U) || ((ipHeaderLength & 0x03U) != 0U) || ((uint16_t)(ipHeaderLength + 8U) > payloadLength))
	{
		return false;
	}

	ipPacketLength = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
	if (ipPacketLength < (uint16_t)(ipHeaderLength + 8U))
	{
		return false;
	}

	if (ipPacketLength > payloadLength)
	{
		return false;
	}

	udpOffset = (uint16_t)ipHeaderLength;
	udpLength = (uint16_t)(((uint16_t)payload[udpOffset + 4U] << 8) | payload[udpOffset + 5U]);
	if ((udpLength < 8U) || ((uint16_t)(udpOffset + udpLength) > ipPacketLength) || ((uint16_t)(udpOffset + udpLength) > payloadLength))
	{
		return false;
	}

	srcPort = (uint16_t)(((uint16_t)payload[udpOffset] << 8) | payload[udpOffset + 1U]);
	dstPort = (uint16_t)(((uint16_t)payload[udpOffset + 2U] << 8) | payload[udpOffset + 3U]);
	hasMotorolaPort = ((srcPort == SMS_MOTOROLA_UDP_PORT) || (dstPort == SMS_MOTOROLA_UDP_PORT));
	hasStandardPort = ((srcPort == SMS_STANDARD_UDP_PORT) || (dstPort == SMS_STANDARD_UDP_PORT));

	if (!hasMotorolaPort && !hasStandardPort)
	{
		return false;
	}

	udpPayloadOffset = (uint16_t)(udpOffset + 8U);
	udpPayloadLength = (uint16_t)(udpLength - 8U);

	if (hasMotorolaPort && (udpPayloadLength >= SMS_MOTOROLA_INTERNAL_HEADER_SIZE))
	{
		uint16_t motorolaTextOffset = (uint16_t)(udpPayloadOffset + SMS_MOTOROLA_INTERNAL_HEADER_SIZE);
		uint16_t motorolaTextLength = (uint16_t)(udpPayloadLength - SMS_MOTOROLA_INTERNAL_HEADER_SIZE);
		uint16_t motorolaTextLengthUtf16 = (uint16_t)(motorolaTextLength & 0xFFFEU);

		if ((payload[udpPayloadOffset] == 0x00U) &&
			((payload[udpPayloadOffset + 2U] == 0xA0U) || (payload[udpPayloadOffset + 2U] == 0xE0U)) &&
			(payload[udpPayloadOffset + 3U] == 0x00U) &&
			(payload[udpPayloadOffset + 5U] == 0x04U) &&
			(payload[udpPayloadOffset + 6U] == 0x0DU) &&
			(payload[udpPayloadOffset + 7U] == 0x00U) &&
			(payload[udpPayloadOffset + 8U] == 0x0AU) &&
			(payload[udpPayloadOffset + 9U] == 0x00U))
		{
			if ((motorolaTextLengthUtf16 >= 2U) && smsDecodeUtf16Payload(&payload[motorolaTextOffset], motorolaTextLengthUtf16, textOut))
			{
				return true;
			}

			if ((motorolaTextLength >= 1U) && smsDecodeAsciiPayload(&payload[motorolaTextOffset], motorolaTextLength, textOut))
			{
				return true;
			}
		}
	}

	if (hasStandardPort && (udpPayloadLength >= 4U))
	{
		uint16_t standardTextOffset = (uint16_t)(udpPayloadOffset + 4U);
		uint16_t standardTextLength = (uint16_t)(udpPayloadLength - 4U);
		uint16_t standardTextLengthUtf16 = (uint16_t)(standardTextLength & 0xFFFEU);

		if ((standardTextLengthUtf16 >= 2U) && smsDecodeUtf16Payload(&payload[standardTextOffset], standardTextLengthUtf16, textOut))
		{
			return true;
		}

		if ((standardTextLength >= 1U) && smsDecodeAsciiPayload(&payload[standardTextOffset], standardTextLength, textOut))
		{
			return true;
		}
	}

	if ((udpPayloadLength >= 4U) && smsDecodeUtf16PayloadSliding(&payload[udpPayloadOffset], udpPayloadLength, textOut))
	{
		return true;
	}

	if ((udpPayloadLength >= 3U) && smsDecodeAsciiPayloadSliding(&payload[udpPayloadOffset], udpPayloadLength, textOut))
	{
		return true;
	}

	return false;
}

static void smsResetRxAssembly(void)
{
	memset(&rxAssembly, 0, sizeof(rxAssembly));
	smsRxTestingLastStoreTick = 0U;
}

static void smsScheduleAckResponse(uint32_t destinationId)
{
#if SMS_RX_TESTING_DISABLE_ACK_TX
	(void)destinationId;
	ackResponseTracking.active = false;
	ackResponseTracking.destinationId = 0U;
	return;
#endif

	if ((destinationId == 0U) || (destinationId > 0x00FFFFFFU))
	{
		return;
	}

	ackResponseTracking.active = true;
	ackResponseTracking.destinationId = destinationId;
}

static bool smsQueueAckResponseMessage(uint32_t destinationId, uint32_t sourceId)
{
	uint16_t crc;

#if SMS_RX_TESTING_DISABLE_ACK_TX
	(void)destinationId;
	(void)sourceId;
	return false;
#endif

	if ((destinationId == 0U) || (destinationId > 0x00FFFFFFU) || (sourceId == 0U) || (sourceId > 0x00FFFFFFU))
	{
		return false;
	}

	memset(&queuedMessage, 0, sizeof(queuedMessage));
	queuedMessage.destinationId = destinationId;
	queuedMessage.sourceId = sourceId;
	queuedMessage.requestAck = false;

	memset(queuedMessage.csbk, 0, sizeof(queuedMessage.csbk));
	queuedMessage.csbk[0] = 0xBDU;
	queuedMessage.csbk[1] = 0x00U;
	queuedMessage.csbk[2] = 0x80U;
	queuedMessage.csbk[3] = 1U;
	queuedMessage.csbk[4] = (uint8_t)((destinationId >> 16) & 0xFFU);
	queuedMessage.csbk[5] = (uint8_t)((destinationId >> 8) & 0xFFU);
	queuedMessage.csbk[6] = (uint8_t)(destinationId & 0xFFU);
	queuedMessage.csbk[7] = (uint8_t)((sourceId >> 16) & 0xFFU);
	queuedMessage.csbk[8] = (uint8_t)((sourceId >> 8) & 0xFFU);
	queuedMessage.csbk[9] = (uint8_t)(sourceId & 0xFFU);
	crc = smsCrc16Ccitt(queuedMessage.csbk, 10U);
	queuedMessage.csbk[10] = (uint8_t)((crc >> 8) & 0xFFU) ^ 0xA5U;
	queuedMessage.csbk[11] = (uint8_t)(crc & 0xFFU) ^ 0xA5U;

	memset(queuedMessage.dataHeader, 0, sizeof(queuedMessage.dataHeader));
	queuedMessage.dataHeader[0] = 0x01U; // Data Response PDU
	queuedMessage.dataHeader[1] = 0x40U; // Match observed SAP/response profile used by hotspot/repeater ACKs
	queuedMessage.dataHeader[2] = (uint8_t)((destinationId >> 16) & 0xFFU);
	queuedMessage.dataHeader[3] = (uint8_t)((destinationId >> 8) & 0xFFU);
	queuedMessage.dataHeader[4] = (uint8_t)(destinationId & 0xFFU);
	queuedMessage.dataHeader[5] = (uint8_t)((sourceId >> 16) & 0xFFU);
	queuedMessage.dataHeader[6] = (uint8_t)((sourceId >> 8) & 0xFFU);
	queuedMessage.dataHeader[7] = (uint8_t)(sourceId & 0xFFU);
	queuedMessage.dataHeader[8] = 0x00U;
	queuedMessage.dataHeader[9] = 0x08U;
	crc = smsCrc16Ccitt(queuedMessage.dataHeader, 10U);
	queuedMessage.dataHeader[10] = (uint8_t)((crc >> 8) & 0xFFU) ^ 0xCCU;
	queuedMessage.dataHeader[11] = (uint8_t)(crc & 0xFFU) ^ 0xCCU;

	queuedMessageValid = true;
	return true;
}

static bool smsHandleIncomingResponsePdu(const uint8_t *frame)
{
	uint8_t dataPacketFormat;
	uint32_t destinationId;
	uint32_t sourceId;

	if ((frame == NULL) || (!outgoingTracking.active))
	{
		return false;
	}

	dataPacketFormat = (uint8_t)(frame[0] & 0x0FU);
	if (dataPacketFormat != 0x01U)
	{
		return false;
	}

	destinationId = (((uint32_t)frame[2] << 16) | ((uint32_t)frame[3] << 8) | frame[4]);
	sourceId = (((uint32_t)frame[5] << 16) | ((uint32_t)frame[6] << 8) | frame[7]);

	if ((destinationId == outgoingTracking.sourceId) && (sourceId == outgoingTracking.destinationId))
	{
		smsNotifyOutgoingAckReceived();
		return true;
	}

	return false;
}

static const uint32_t smsCrc32Table[256] = {
	0x00000000U, 0x04C11DB7U, 0x09823B6EU, 0x0D4326D9U, 0x130476DCU, 0x17C56B6BU, 0x1A864DB2U, 0x1E475005U,
	0x2608EDB8U, 0x22C9F00FU, 0x2F8AD6D6U, 0x2B4BCB61U, 0x350C9B64U, 0x31CD86D3U, 0x3C8EA00AU, 0x384FBDBDU,
	0x4C11DB70U, 0x48D0C6C7U, 0x4593E01EU, 0x4152FDA9U, 0x5F15ADACU, 0x5BD4B01BU, 0x569796C2U, 0x52568B75U,
	0x6A1936C8U, 0x6ED82B7FU, 0x639B0DA6U, 0x675A1011U, 0x791D4014U, 0x7DDC5DA3U, 0x709F7B7AU, 0x745E66CDU,
	0x9823B6E0U, 0x9CE2AB57U, 0x91A18D8EU, 0x95609039U, 0x8B27C03CU, 0x8FE6DD8BU, 0x82A5FB52U, 0x8664E6E5U,
	0xBE2B5B58U, 0xBAEA46EFU, 0xB7A96036U, 0xB3687D81U, 0xAD2F2D84U, 0xA9EE3033U, 0xA4AD16EAU, 0xA06C0B5DU,
	0xD4326D90U, 0xD0F37027U, 0xDDB056FEU, 0xD9714B49U, 0xC7361B4CU, 0xC3F706FBU, 0xCEB42022U, 0xCA753D95U,
	0xF23A8028U, 0xF6FB9D9FU, 0xFBB8BB46U, 0xFF79A6F1U, 0xE13EF6F4U, 0xE5FFEB43U, 0xE8BCCD9AU, 0xEC7DD02DU,
	0x34867077U, 0x30476DC0U, 0x3D044B19U, 0x39C556AEU, 0x278206ABU, 0x23431B1CU, 0x2E003DC5U, 0x2AC12072U,
	0x128E9DCFU, 0x164F8078U, 0x1B0CA6A1U, 0x1FCDBB16U, 0x018AEB13U, 0x054BF6A4U, 0x0808D07DU, 0x0CC9CDCAU,
	0x7897AB07U, 0x7C56B6B0U, 0x71159069U, 0x75D48DDEU, 0x6B93DDDBU, 0x6F52C06CU, 0x6211E6B5U, 0x66D0FB02U,
	0x5E9F46BFU, 0x5A5E5B08U, 0x571D7DD1U, 0x53DC6066U, 0x4D9B3063U, 0x495A2DD4U, 0x44190B0DU, 0x40D816BAU,
	0xACA5C697U, 0xA864DB20U, 0xA527FDF9U, 0xA1E6E04EU, 0xBFA1B04BU, 0xBB60ADFCU, 0xB6238B25U, 0xB2E29692U,
	0x8AAD2B2FU, 0x8E6C3698U, 0x832F1041U, 0x87EE0DF6U, 0x99A95DF3U, 0x9D684044U, 0x902B669DU, 0x94EA7B2AU,
	0xE0B41DE7U, 0xE4750050U, 0xE9362689U, 0xEDF73B3EU, 0xF3B06B3BU, 0xF771768CU, 0xFA325055U, 0xFEF34DE2U,
	0xC6BCF05FU, 0xC27DEDE8U, 0xCF3ECB31U, 0xCBFFD686U, 0xD5B88683U, 0xD1799B34U, 0xDC3ABDEDU, 0xD8FBA05AU,
	0x690CE0EEU, 0x6DCDFD59U, 0x608EDB80U, 0x644FC637U, 0x7A089632U, 0x7EC98B85U, 0x738AAD5CU, 0x774BB0EBU,
	0x4F040D56U, 0x4BC510E1U, 0x46863638U, 0x42472B8FU, 0x5C007B8AU, 0x58C1663DU, 0x558240E4U, 0x51435D53U,
	0x251D3B9EU, 0x21DC2629U, 0x2C9F00F0U, 0x285E1D47U, 0x36194D42U, 0x32D850F5U, 0x3F9B762CU, 0x3B5A6B9BU,
	0x0315D626U, 0x07D4CB91U, 0x0A97ED48U, 0x0E56F0FFU, 0x1011A0FAU, 0x14D0BD4DU, 0x19939B94U, 0x1D528623U,
	0xF12F560EU, 0xF5EE4BB9U, 0xF8AD6D60U, 0xFC6C70D7U, 0xE22B20D2U, 0xE6EA3D65U, 0xEBA91BBCU, 0xEF68060BU,
	0xD727BBB6U, 0xD3E6A601U, 0xDEA580D8U, 0xDA649D6FU, 0xC423CD6AU, 0xC0E2D0DDU, 0xCDA1F604U, 0xC960EBB3U,
	0xBD3E8D7EU, 0xB9FF90C9U, 0xB4BCB610U, 0xB07DABA7U, 0xAE3AFBA2U, 0xAAFBE615U, 0xA7B8C0CCU, 0xA379DD7BU,
	0x9B3660C6U, 0x9FF77D71U, 0x92B45BA8U, 0x9675461FU, 0x8832161AU, 0x8CF30BADU, 0x81B02D74U, 0x857130C3U,
	0x5D8A9099U, 0x594B8D2EU, 0x5408ABF7U, 0x50C9B640U, 0x4E8EE645U, 0x4A4FFBF2U, 0x470CDD2BU, 0x43CDC09CU,
	0x7B827D21U, 0x7F436096U, 0x7200464FU, 0x76C15BF8U, 0x68860BFDU, 0x6C47164AU, 0x61043093U, 0x65C52D24U,
	0x119B4BE9U, 0x155A565EU, 0x18197087U, 0x1CD86D30U, 0x029F3D35U, 0x065E2082U, 0x0B1D065BU, 0x0FDC1BECU,
	0x3793A651U, 0x3352BBE6U, 0x3E119D3FU, 0x3AD08088U, 0x2497D08DU, 0x2056CD3AU, 0x2D15EBE3U, 0x29D4F654U,
	0xC5A92679U, 0xC1683BCEU, 0xCC2B1D17U, 0xC8EA00A0U, 0xD6AD50A5U, 0xD26C4D12U, 0xDF2F6BCBU, 0xDBEE767CU,
	0xE3A1CBC1U, 0xE760D676U, 0xEA23F0AFU, 0xEEE2ED18U, 0xF0A5BD1DU, 0xF464A0AAU, 0xF9278673U, 0xFDE69BC4U,
	0x89B8FD09U, 0x8D79E0BEU, 0x803AC667U, 0x84FBDBD0U, 0x9ABC8BD5U, 0x9E7D9662U, 0x933EB0BBU, 0x97FFAD0CU,
	0xAFB010B1U, 0xAB710D06U, 0xA6322BDFU, 0xA2F33668U, 0xBCB4666DU, 0xB8757BDAU, 0xB5365D03U, 0xB1F740B4U
};

static uint32_t smsCrc32Compute(const uint8_t *data, uint16_t length)
{
	uint32_t crc = 0U;

	for (uint16_t i = 0U; (i + 1U) < length; i += 2U)
	{
		crc = smsCrc32Table[(data[i + 1U] ^ ((crc >> 24) & 0xFFU))] ^ (crc << 8);
		crc = smsCrc32Table[(data[i] ^ ((crc >> 24) & 0xFFU))] ^ (crc << 8);
	}

	if ((length & 1U) != 0U)
	{
		crc = smsCrc32Table[(0x00U ^ ((crc >> 24) & 0xFFU))] ^ (crc << 8);
		crc = smsCrc32Table[(data[length - 1U] ^ ((crc >> 24) & 0xFFU))] ^ (crc << 8);
	}

	return crc;
}

static uint16_t smsIpHeaderChecksum(const uint8_t *header, uint16_t length)
{
	uint32_t sum = 0U;

	for (uint16_t i = 0U; i < length; i += 2U)
	{
		sum += ((uint32_t)header[i] << 8);
		if ((i + 1U) < length)
		{
			sum += (uint32_t)header[i + 1U];
		}
	}

	while ((sum >> 16) != 0U)
	{
		sum = (sum & 0xFFFFU) + (sum >> 16);
	}

	return (uint16_t)(~sum & 0xFFFFU);
}

static uint16_t smsUdpChecksum(const uint8_t *ipPacket, uint16_t udpLength)
{
	uint32_t sum = 0U;
	uint16_t result;
	uint8_t ipHeaderLength;

	if (ipPacket == NULL)
	{
		return 0x0000U;
	}

	ipHeaderLength = (uint8_t)((ipPacket[0] & 0x0FU) * 4U);
	if (ipHeaderLength < 20U)
	{
		return 0x0000U;
	}

	for (uint16_t i = 12U; i < 20U; i += 2U)
	{
		sum += ((uint32_t)ipPacket[i] << 8) + (uint32_t)ipPacket[i + 1U];
	}
	sum += 0x0011U;
	sum += (uint32_t)udpLength;

	for (uint16_t i = 0U; i < udpLength; i += 2U)
	{
		sum += ((uint32_t)ipPacket[ipHeaderLength + i] << 8);
		if ((i + 1U) < udpLength)
		{
			sum += (uint32_t)ipPacket[ipHeaderLength + i + 1U];
		}
	}

	while ((sum >> 16) != 0U)
	{
		sum = (sum & 0xFFFFU) + (sum >> 16);
	}

	result = (uint16_t)(~sum & 0xFFFFU);
	return (result == 0x0000U) ? 0xFFFFU : result;
}

static void smsBuildIpHeader(uint8_t *packet, uint16_t ipPacketLength, uint32_t sourceId, uint32_t destinationId)
{
	uint16_t checksum;

	packet[0]  = 0x45U;
	packet[1]  = 0x00U;
	packet[2]  = (uint8_t)((ipPacketLength >> 8) & 0xFFU);
	packet[3]  = (uint8_t)(ipPacketLength & 0xFFU);
	packet[4]  = (uint8_t)((smsIpSequenceNumber >> 8) & 0xFFU);
	packet[5]  = (uint8_t)(smsIpSequenceNumber & 0xFFU);
	smsIpSequenceNumber++;
	packet[6]  = 0x00U;
	packet[7]  = 0x00U;
	packet[8]  = 0x01U;
	packet[9]  = 0x11U;
	packet[10] = 0x00U;
	packet[11] = 0x00U;
	packet[12] = 0x0CU;
	packet[13] = (uint8_t)((sourceId >> 16) & 0xFFU);
	packet[14] = (uint8_t)((sourceId >> 8) & 0xFFU);
	packet[15] = (uint8_t)(sourceId & 0xFFU);
	packet[16] = 0x0CU;
	packet[17] = (uint8_t)((destinationId >> 16) & 0xFFU);
	packet[18] = (uint8_t)((destinationId >> 8) & 0xFFU);
	packet[19] = (uint8_t)(destinationId & 0xFFU);

	checksum = smsIpHeaderChecksum(packet, 20U);
	packet[10] = (uint8_t)((checksum >> 8) & 0xFFU);
	packet[11] = (uint8_t)(checksum & 0xFFU);
}

static void smsBuildMotorolaUdpHeader(uint8_t *packet, uint16_t textByteLength, uint16_t ipSequence)
{
	uint16_t udpLength = (uint16_t)(textByteLength + 18U);
	uint16_t internalLength = (uint16_t)(textByteLength + 8U);
	uint16_t checksum;

	packet[20] = (uint8_t)((SMS_MOTOROLA_UDP_PORT >> 8) & 0xFFU);
	packet[21] = (uint8_t)(SMS_MOTOROLA_UDP_PORT & 0xFFU);
	packet[22] = (uint8_t)((SMS_MOTOROLA_UDP_PORT >> 8) & 0xFFU);
	packet[23] = (uint8_t)(SMS_MOTOROLA_UDP_PORT & 0xFFU);
	packet[24] = (uint8_t)((udpLength >> 8) & 0xFFU);
	packet[25] = (uint8_t)(udpLength & 0xFFU);
	packet[26] = 0x00U;
	packet[27] = 0x00U;
	packet[28] = 0x00U;
	packet[29] = (uint8_t)(internalLength & 0xFFU);
	packet[30] = 0xE0U;
	packet[31] = 0x00U;
	packet[32] = (uint8_t)((ipSequence & 0xFFU) | 0x80U);
	packet[33] = 0x04U;
	packet[34] = 0x0DU;
	packet[35] = 0x00U;
	packet[36] = 0x0AU;
	packet[37] = 0x00U;

	checksum = smsUdpChecksum(packet, udpLength);
	packet[26] = (uint8_t)((checksum >> 8) & 0xFFU);
	packet[27] = (uint8_t)(checksum & 0xFFU);
}

static smsPackResult_t smsBuildMotorolaPayload(uint32_t destinationId, uint32_t sourceId, const char *text, uint8_t *payload, uint16_t *payloadLength, uint8_t *padOctetCount)
{
	uint8_t utf16Payload[SMS_MAX_TX_UTF16_PAYLOAD_BYTES];
	uint16_t textByteLength = 0U;
	uint16_t ipPacketLength;
	uint16_t crcOffset;
	uint32_t crc32;
	uint16_t currentIpSeq;
	smsPackResult_t result;

	if ((payload == NULL) || (payloadLength == NULL) || (padOctetCount == NULL))
	{
		return SMS_PACK_ERROR_EMPTY;
	}

	result = smsConvertTextToUtf16LeUpper(text, utf16Payload, &textByteLength);
	if (result != SMS_PACK_OK)
	{
		return result;
	}

	ipPacketLength = (uint16_t)(SMS_MOTOROLA_TEXT_OFFSET + textByteLength);
	*padOctetCount = (uint8_t)((SMS_BLOCK_DATA_BYTES - ((ipPacketLength + SMS_STANDARD_CRC32_BYTES) % SMS_BLOCK_DATA_BYTES)) % SMS_BLOCK_DATA_BYTES);
	crcOffset = (uint16_t)(ipPacketLength + *padOctetCount);
	*payloadLength = (uint16_t)(crcOffset + SMS_STANDARD_CRC32_BYTES);

	if (*payloadLength > SMS_MAX_TX_TRANSPORT_BYTES)
	{
		return SMS_PACK_ERROR_TOO_LONG;
	}

	memset(payload, 0, SMS_MAX_TX_TRANSPORT_BYTES);
	currentIpSeq = smsIpSequenceNumber;
	smsBuildIpHeader(payload, ipPacketLength, sourceId, destinationId);
	smsBuildMotorolaUdpHeader(payload, textByteLength, currentIpSeq);
	memcpy(&payload[SMS_MOTOROLA_TEXT_OFFSET], utf16Payload, textByteLength);

	crc32 = smsCrc32Compute(payload, crcOffset);
	payload[crcOffset] = (uint8_t)(crc32 & 0xFFU);
	payload[crcOffset + 1U] = (uint8_t)((crc32 >> 8) & 0xFFU);
	payload[crcOffset + 2U] = (uint8_t)((crc32 >> 16) & 0xFFU);
	payload[crcOffset + 3U] = (uint8_t)((crc32 >> 24) & 0xFFU);

	return SMS_PACK_OK;
}

static bool smsDecodeMotorolaPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, char *textOut)
{
	uint8_t ipHeaderLength;
	uint16_t udpOffset;
	uint16_t motorolaHeaderOffset;
	uint16_t textOffset;
	uint16_t ipPacketLength;
	uint16_t udpLength;
	uint16_t srcPort;
	uint16_t dstPort;
	uint16_t headerChecksum;
	uint16_t udpChecksum;
	uint16_t textByteLength;
	uint16_t internalLength;
	uint32_t expectedCrc32;
	uint32_t receivedCrc32;
	uint8_t headerCopy[60U];
	uint8_t packetCopy[SMS_MAX_RX_TRANSPORT_BYTES];

	if ((payload == NULL) || (textOut == NULL) || (totalLength < (20U + 8U + SMS_STANDARD_CRC32_BYTES)))
	{
		return false;
	}

	ipHeaderLength = (uint8_t)((payload[0] & 0x0FU) * 4U);
	if ((ipHeaderLength < 20U) || ((ipHeaderLength & 0x03U) != 0U))
	{
		return false;
	}

	udpOffset = (uint16_t)ipHeaderLength;
	motorolaHeaderOffset = (uint16_t)(udpOffset + 8U);
	textOffset = (uint16_t)(motorolaHeaderOffset + 10U);

	ipPacketLength = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
	if ((ipPacketLength < textOffset) ||
		(ipPacketLength > (totalLength - SMS_STANDARD_CRC32_BYTES)) ||
		((uint16_t)(ipPacketLength + padOctets + SMS_STANDARD_CRC32_BYTES) != totalLength))
	{
		return false;
	}

	if (((payload[0] & 0xF0U) != 0x40U) || (payload[9] != SMS_MOTOROLA_IPV4_PROTOCOL))
	{
		return false;
	}

	memcpy(headerCopy, payload, ipHeaderLength);
	headerChecksum = (uint16_t)(((uint16_t)payload[10] << 8) | payload[11]);
	headerCopy[10] = 0x00U;
	headerCopy[11] = 0x00U;
	if (smsIpHeaderChecksum(headerCopy, ipHeaderLength) != headerChecksum)
	{
		return false;
	}

	udpLength = (uint16_t)(((uint16_t)payload[udpOffset + 4U] << 8) | payload[udpOffset + 5U]);
	if ((udpLength < 18U) || ((uint16_t)(udpOffset + udpLength) != ipPacketLength))
	{
		return false;
	}

	srcPort = (uint16_t)(((uint16_t)payload[udpOffset] << 8) | payload[udpOffset + 1U]);
	dstPort = (uint16_t)(((uint16_t)payload[udpOffset + 2U] << 8) | payload[udpOffset + 3U]);
	if ((srcPort != SMS_MOTOROLA_UDP_PORT) && (dstPort != SMS_MOTOROLA_UDP_PORT))
	{
		return false;
	}

	memcpy(packetCopy, payload, ipPacketLength);
	udpChecksum = (uint16_t)(((uint16_t)payload[udpOffset + 6U] << 8) | payload[udpOffset + 7U]);
	packetCopy[udpOffset + 6U] = 0x00U;
	packetCopy[udpOffset + 7U] = 0x00U;
	if ((udpChecksum != 0U) && (smsUdpChecksum(packetCopy, udpLength) != udpChecksum))
	{
		return false;
	}

	if (payload[motorolaHeaderOffset] != 0x00U)
	{
		return false;
	}

	internalLength = (uint16_t)payload[motorolaHeaderOffset + 1U];
	textByteLength = (uint16_t)(udpLength - 18U);
	if ((internalLength != (textByteLength + 8U)) ||
		((payload[motorolaHeaderOffset + 2U] != 0xA0U) && (payload[motorolaHeaderOffset + 2U] != 0xE0U)) ||
		(payload[motorolaHeaderOffset + 3U] != 0x00U))
	{
		return false;
	}

	if ((payload[motorolaHeaderOffset + 5U] != 0x04U) || (payload[motorolaHeaderOffset + 6U] != 0x0DU) || (payload[motorolaHeaderOffset + 7U] != 0x00U) ||
		(payload[motorolaHeaderOffset + 8U] != 0x0AU) || (payload[motorolaHeaderOffset + 9U] != 0x00U))
	{
		return false;
	}

	for (uint16_t i = ipPacketLength; i < (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES); i++)
	{
		if (payload[i] != 0x00U)
		{
			return false;
		}
	}

	expectedCrc32 = smsCrc32Compute(payload, (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES));
	receivedCrc32 = ((uint32_t)payload[totalLength - 1U] << 24) |
		((uint32_t)payload[totalLength - 2U] << 16) |
		((uint32_t)payload[totalLength - 3U] << 8) |
		(uint32_t)payload[totalLength - 4U];
	if (expectedCrc32 != receivedCrc32)
	{
		return false;
	}

	if (((textByteLength & 0x01U) != 0U) || (textByteLength == 0U) ||
		(textByteLength > SMS_MAX_RX_UTF16_PAYLOAD_BYTES) ||
		((uint16_t)(textOffset + textByteLength) != ipPacketLength))
	{
		return false;
	}

	return smsDecodeUtf16Payload(&payload[textOffset], textByteLength, textOut);
}

static bool smsDecodeStandardPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, char *textOut)
{
	uint8_t ipHeaderLength;
	uint16_t udpOffset;
	uint16_t textOffset;
	uint16_t ipPacketLength;
	uint16_t udpLength;
	uint16_t srcPort;
	uint16_t dstPort;
	uint16_t headerChecksum;
	uint16_t udpChecksum;
	uint16_t textByteLength;
	uint32_t expectedCrc32;
	uint32_t receivedCrc32;
	uint8_t headerCopy[60U];
	uint8_t packetCopy[SMS_MAX_RX_TRANSPORT_BYTES];

	if ((payload == NULL) || (textOut == NULL) || (totalLength < (20U + 8U + SMS_STANDARD_CRC32_BYTES)))
	{
		return false;
	}

	ipHeaderLength = (uint8_t)((payload[0] & 0x0FU) * 4U);
	if ((ipHeaderLength < 20U) || ((ipHeaderLength & 0x03U) != 0U))
	{
		return false;
	}

	udpOffset = (uint16_t)ipHeaderLength;
	textOffset = (uint16_t)(udpOffset + 12U);

	ipPacketLength = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
	if ((ipPacketLength < textOffset) ||
		(ipPacketLength > (totalLength - SMS_STANDARD_CRC32_BYTES)) ||
		((uint16_t)(ipPacketLength + padOctets + SMS_STANDARD_CRC32_BYTES) != totalLength))
	{
		return false;
	}

	if (((payload[0] & 0xF0U) != 0x40U) || (payload[9] != SMS_MOTOROLA_IPV4_PROTOCOL))
	{
		return false;
	}

	memcpy(headerCopy, payload, ipHeaderLength);
	headerChecksum = (uint16_t)(((uint16_t)payload[10] << 8) | payload[11]);
	headerCopy[10] = 0x00U;
	headerCopy[11] = 0x00U;
	if (smsIpHeaderChecksum(headerCopy, ipHeaderLength) != headerChecksum)
	{
		return false;
	}

	udpLength = (uint16_t)(((uint16_t)payload[udpOffset + 4U] << 8) | payload[udpOffset + 5U]);
	if ((udpLength < 12U) || ((uint16_t)(udpOffset + udpLength) != ipPacketLength))
	{
		return false;
	}

	srcPort = (uint16_t)(((uint16_t)payload[udpOffset] << 8) | payload[udpOffset + 1U]);
	dstPort = (uint16_t)(((uint16_t)payload[udpOffset + 2U] << 8) | payload[udpOffset + 3U]);
	if ((srcPort != SMS_STANDARD_UDP_PORT) && (dstPort != SMS_STANDARD_UDP_PORT))
	{
		return false;
	}

	memcpy(packetCopy, payload, ipPacketLength);
	udpChecksum = (uint16_t)(((uint16_t)payload[udpOffset + 6U] << 8) | payload[udpOffset + 7U]);
	packetCopy[udpOffset + 6U] = 0x00U;
	packetCopy[udpOffset + 7U] = 0x00U;
	if ((udpChecksum != 0U) && (smsUdpChecksum(packetCopy, udpLength) != udpChecksum))
	{
		return false;
	}

	for (uint16_t i = ipPacketLength; i < (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES); i++)
	{
		if (payload[i] != 0x00U)
		{
			return false;
		}
	}

	expectedCrc32 = smsCrc32Compute(payload, (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES));
	receivedCrc32 = ((uint32_t)payload[totalLength - 1U] << 24) |
		((uint32_t)payload[totalLength - 2U] << 16) |
		((uint32_t)payload[totalLength - 3U] << 8) |
		(uint32_t)payload[totalLength - 4U];
	if (expectedCrc32 != receivedCrc32)
	{
		return false;
	}

	textByteLength = (uint16_t)(udpLength - 12U);
	if (((textByteLength & 0x01U) != 0U) || (textByteLength == 0U) ||
		(textByteLength > SMS_MAX_RX_UTF16_PAYLOAD_BYTES) ||
		((uint16_t)(textOffset + textByteLength) != ipPacketLength))
	{
		return false;
	}

	return smsDecodeUtf16Payload(&payload[textOffset], textByteLength, textOut);
}

static uint16_t smsCrc16Ccitt(const uint8_t *data, uint8_t length)
{
	uint16_t crc = 0x0000U;

	for (uint8_t index = 0U; index < length; index++)
	{
		crc ^= ((uint16_t)data[index] << 8);
		for (uint8_t bit = 0U; bit < 8U; bit++)
		{
			if ((crc & 0x8000U) != 0U)
			{
				crc = (uint16_t)((crc << 1) ^ 0x1021U);
			}
			else
			{
				crc <<= 1;
			}
		}
	}

	return crc;
}

static void smsBuildCsbk(smsPreparedMessage_t *message)
{
	uint16_t crc;

	memset(message->csbk, 0, sizeof(message->csbk));
	message->csbk[0] = 0xBDU;                                              // CSBK opcode 0x3D + LAST flag
	message->csbk[1] = 0x00U;                                              // Reserved
	message->csbk[2] = 0x80U;                                              // DATA=1 (data to follow), GROUP=0 (private)
	message->csbk[3] = (uint8_t)(message->blockCount + 1U);                // Blocks to follow: N data blocks + 1 data header
	message->csbk[4] = (uint8_t)((message->destinationId >> 16) & 0xFFU);
	message->csbk[5] = (uint8_t)((message->destinationId >> 8) & 0xFFU);
	message->csbk[6] = (uint8_t)(message->destinationId & 0xFFU);
	message->csbk[7] = (uint8_t)((message->sourceId >> 16) & 0xFFU);
	message->csbk[8] = (uint8_t)((message->sourceId >> 8) & 0xFFU);
	message->csbk[9] = (uint8_t)(message->sourceId & 0xFFU);

	crc = smsCrc16Ccitt(message->csbk, 10U);
	message->csbk[10] = (uint8_t)((crc >> 8) & 0xFFU) ^ 0xA5U;            // DMR CSBK CRC mask
	message->csbk[11] = (uint8_t)(crc & 0xFFU) ^ 0xA5U;
}

static void smsBuildDataHeader(smsPreparedMessage_t *message)
{
	uint16_t crc;

	memset(message->dataHeader, 0, sizeof(message->dataHeader));
	message->dataHeader[0] = 0x42U | (uint8_t)(message->padOctetCount & 0x10U);
	message->dataHeader[1] = 0x40U | (uint8_t)(message->padOctetCount & 0x0FU);  // SAP=IP(0x04) + pad[3:0]
	message->dataHeader[2] = (uint8_t)((message->destinationId >> 16) & 0xFFU);
	message->dataHeader[3] = (uint8_t)((message->destinationId >> 8) & 0xFFU);
	message->dataHeader[4] = (uint8_t)(message->destinationId & 0xFFU);
	message->dataHeader[5] = (uint8_t)((message->sourceId >> 16) & 0xFFU);
	message->dataHeader[6] = (uint8_t)((message->sourceId >> 8) & 0xFFU);
	message->dataHeader[7] = (uint8_t)(message->sourceId & 0xFFU);
	message->dataHeader[8] = (uint8_t)(0x80U | (message->blockCount & 0x7FU));   // Full packet, N blocks
	message->dataHeader[9] = 0x00U;                                               // Fragment #0

	crc = smsCrc16Ccitt(message->dataHeader, 10U);
	message->dataHeader[10] = (uint8_t)((crc >> 8) & 0xFFU) ^ 0xCCU;            // DMR Data Header CRC mask
	message->dataHeader[11] = (uint8_t)(crc & 0xFFU) ^ 0xCCU;
}

static smsPackResult_t smsConvertTextToUtf16LeUpper(const char *text, uint8_t *payload, uint16_t *payloadLength)
{
	uint16_t index = 0;

	while (*text != 0)
	{
		unsigned char character = (unsigned char)(*text++);

		if (index >= SMS_MAX_TX_UTF16_PAYLOAD_BYTES)
		{
			return SMS_PACK_ERROR_TOO_LONG;
		}

		if ((character < 0x20U) || ((character > 0x7EU) && (character < 0xA0U)))
		{
			if ((character != '\r') && (character != '\n') && (character != '\t'))
			{
				return SMS_PACK_ERROR_UNSUPPORTED_CHAR;
			}

			if (character == '\t')
			{
				character = ' ';
			}
		}

		if ((character >= 'a') && (character <= 'z'))
		{
			character = (unsigned char)(character - ('a' - 'A'));
		}

		payload[index++] = character;
		payload[index++] = 0x00U;
	}

	*payloadLength = index;
	return ((index == 0U) ? SMS_PACK_ERROR_EMPTY : SMS_PACK_OK);
}

smsPackResult_t smsPackMessage(uint32_t destinationId, uint32_t sourceId, const char *text, smsPreparedMessage_t *message)
{
	smsPackResult_t result;
	uint8_t payload[SMS_MAX_TX_DATA_BLOCKS * SMS_BLOCK_DATA_BYTES];
	uint16_t payloadLength = 0;
	uint8_t blockCount;
	uint16_t offset;

	if ((message == NULL) || (text == NULL))
	{
		return SMS_PACK_ERROR_EMPTY;
	}

	if ((destinationId == 0U) || (destinationId > 0x00FFFFFFU))
	{
		return SMS_PACK_ERROR_INVALID_DEST;
	}

	if ((sourceId == 0U) || (sourceId > 0x00FFFFFFU))
	{
		return SMS_PACK_ERROR_INVALID_SRC;
	}

	memset(message, 0, sizeof(*message));
	message->destinationId = destinationId;
	message->sourceId = sourceId;
	message->requestAck = true;
	memset(payload, 0, sizeof(payload));

	result = smsBuildMotorolaPayload(destinationId, sourceId, text, payload, &payloadLength, &message->padOctetCount);
	if (result != SMS_PACK_OK)
	{
		return result;
	}

	message->payloadLength = payloadLength;
	blockCount = (uint8_t)(payloadLength / SMS_BLOCK_DATA_BYTES);
	message->blockCount = blockCount;

	offset = 0U;
	for (uint8_t block = 0U; block < blockCount; block++)
	{
		uint8_t bytesToCopy = SMS_BLOCK_DATA_BYTES;
		if ((payloadLength - offset) < bytesToCopy)
		{
			bytesToCopy = (uint8_t)(payloadLength - offset);
		}

		memcpy(message->blocks[block], &payload[offset], bytesToCopy);
		offset += bytesToCopy;
	}

	smsBuildCsbk(message);
	smsBuildDataHeader(message);

	return SMS_PACK_OK;
}

smsPackResult_t smsQueueMessage(uint32_t destinationId, uint32_t sourceId, const char *text)
{
	smsPackResult_t result = smsPackMessage(destinationId, sourceId, text, &queuedMessage);
	queuedMessageValid = (result == SMS_PACK_OK);
	return result;
}

bool smsHasQueuedMessage(void)
{
	return queuedMessageValid;
}

const smsPreparedMessage_t *smsGetQueuedMessage(void)
{
	return (queuedMessageValid ? &queuedMessage : NULL);
}

void smsClearQueuedMessage(void)
{
	queuedMessageValid = false;
	memset(&queuedMessage, 0, sizeof(queuedMessage));
}

void smsRegisterOutgoingMessage(uint32_t destinationId, uint32_t sourceId, const char *text)
{
	if ((text == NULL) || (text[0] == 0) || (destinationId == 0U) || (sourceId == 0U))
	{
		return;
	}

	outgoingTracking.active = true;
	outgoingTracking.destinationId = destinationId;
	outgoingTracking.sourceId = sourceId;
	strncpy(outgoingTracking.text, text, SMS_MAX_TX_TEXT_LENGTH);
	outgoingTracking.text[SMS_MAX_TX_TEXT_LENGTH] = 0;
	ticksTimerStart(&outgoingTracking.ackTimer, SMS_TX_ACK_TIMEOUT_MS);
	smsSetPendingTxEvent(SMS_TX_EVENT_SENDING);
}

void smsNotifyOutgoingAckReceived(void)
{
	if (!outgoingTracking.active)
	{
		return;
	}

	outgoingTracking.active = false;
	smsSetPendingTxEvent(SMS_TX_EVENT_ACK);
}

void smsNotifyOutgoingRejected(void)
{
	if (!outgoingTracking.active)
	{
		return;
	}

	outgoingTracking.active = false;
	smsSetPendingTxEvent(SMS_TX_EVENT_REJECTED);
}

void smsNotifyOutgoingNoRepeater(void)
{
	if (!outgoingTracking.active)
	{
		return;
	}

	outgoingTracking.active = false;
	smsSetPendingTxEvent(SMS_TX_EVENT_NO_REPEATER);
}

void smsNotifyOutgoingSent(void)
{
	if (!outgoingTracking.active)
	{
		return;
	}

	outgoingTracking.active = false;
	smsSetPendingTxEvent(SMS_TX_EVENT_SENT);
}

bool smsRetryLastOutgoingMessage(void)
{
	smsPackResult_t result;

	if ((outgoingTracking.destinationId == 0U) || (outgoingTracking.sourceId == 0U) || (outgoingTracking.text[0] == 0))
	{
		return false;
	}

	result = smsQueueMessage(outgoingTracking.destinationId, outgoingTracking.sourceId, outgoingTracking.text);
	if (result != SMS_PACK_OK)
	{
		return false;
	}

	if (HRC6000StartQueuedSMS() == false)
	{
		smsClearQueuedMessage();
		return false;
	}

	outgoingTracking.active = true;
	ticksTimerStart(&outgoingTracking.ackTimer, SMS_TX_ACK_TIMEOUT_MS);
	smsSetPendingTxEvent(SMS_TX_EVENT_SENDING);
	return true;
}

smsTxEvent_t smsConsumeTxEvent(void)
{
	smsTxEvent_t event = pendingTxEvent;
	pendingTxEvent = SMS_TX_EVENT_NONE;
	return event;
}

void smsTick(void)
{
#if SMS_RX_TESTING_DISABLE_ACK_TX
	ackResponseTracking.active = false;
	ackResponseTracking.destinationId = 0U;
#endif

	if (smsRxAssemblyIsActive() && (smsRxTestingLastStoreTick != 0U))
	{
		uint32_t now = ticksGetMillis();

		if ((uint32_t)(now - smsRxTestingLastStoreTick) > SMS_RX_ASSEMBLY_TIMEOUT_MS)
		{
			if (SMS_RX_TESTING_OPEN_DOOR)
			{
				if (rxAssembly.receivedBlocks == 0U)
				{
					smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_NO_H07");
				}
				else
				{
					smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_PARTIAL_D07");
				}
			}

			if (!SMS_RX_TESTING_ACK_MINIMAL && smsRxAssemblyIsResponseRequested() && (rxAssembly.sourceId != trxDMRID))
			{
				smsScheduleAckResponse(rxAssembly.sourceId);
			}

			smsResetRxAssembly();
		}
	}

	if (!SMS_RX_TESTING_DISABLE_ACK_TX && ackResponseTracking.active && (trxDMRID != 0U) && !smsHasQueuedMessage() && !HRC6000IsSendingSMS() && !HRC6000IRQHandlerIsRunning())
	{
		if (smsQueueAckResponseMessage(ackResponseTracking.destinationId, trxDMRID))
		{
			if (HRC6000StartQueuedSMS())
			{
				ackResponseTracking.active = false;
			}
			else
			{
				smsClearQueuedMessage();
			}
		}
	}

	if (!outgoingTracking.active)
	{
		return;
	}

	if (!ticksTimerHasExpired(&outgoingTracking.ackTimer))
	{
		return;
	}

	if (HRC6000IsSendingSMS() || HRC6000IRQHandlerIsRunning())
	{
		return;
	}

	outgoingTracking.active = false;
	smsSetPendingTxEvent(SMS_TX_EVENT_TIMEOUT);
}

bool smsHandleReceivedDataFrame(uint8_t dataType, const uint8_t *frame)
{
	if (frame == NULL)
	{
		return false;
	}

	if (dataType == 0x06U)
	{
		uint8_t dataPacketFormat = (uint8_t)(frame[0] & 0x0FU);
		bool responseRequested = (((frame[0] & 0x40U) != 0U) || (dataPacketFormat == 0x02U) || (dataPacketFormat == 0x03U));
		uint8_t sapType;
		uint8_t blocks;
		bool fullMessageFlag;
		uint8_t pad = 0xFFU;
		uint32_t headerIdA;
		uint32_t headerIdB;
		uint32_t peerId;

		if (smsHandleIncomingResponsePdu(frame))
		{
			return true;
		}

		if (dataPacketFormat == 0x01U)
		{
			// Unmatched response PDU: consume without disturbing an active assembled SMS.
			return true;
		}

		if ((dataPacketFormat != 0x02U) && (dataPacketFormat != 0x03U))
		{
			return false;
		}

		sapType = (uint8_t)(frame[1] & 0xF0U);
		blocks = (uint8_t)(frame[8] & 0x7FU);
		fullMessageFlag = ((frame[8] & 0x80U) != 0U);
		headerIdA = (((uint32_t)frame[2] << 16) | ((uint32_t)frame[3] << 8) | frame[4]);
		headerIdB = (((uint32_t)frame[5] << 16) | ((uint32_t)frame[6] << 8) | frame[7]);
		peerId = headerIdB;
		if ((headerIdA == trxDMRID) && (headerIdB != trxDMRID))
		{
			peerId = headerIdB;
		}
		else if ((headerIdB == trxDMRID) && (headerIdA != trxDMRID))
		{
			peerId = headerIdA;
		}
		else if (headerIdA != 0U)
		{
			peerId = headerIdA;
		}
		if (dataPacketFormat == 0x02U)
		{
			pad = (uint8_t)((frame[0] & 0x10U) | (frame[1] & 0x0FU));
		}

		if ((sapType != 0x40U) || (blocks == 0U) || (blocks > SMS_MAX_RX_DATA_BLOCKS) || ((dataPacketFormat == 0x02U) && (pad >= SMS_BLOCK_DATA_BYTES)))
		{
			return false;
		}

		if (settingsIsOptionBitSet(BIT_SMS_FILTER_INCOMING_PC) && (!SMS_RX_TESTING_OPEN_DOOR))
		{
			if ((headerIdA != trxDMRID) && (headerIdB != trxDMRID))
			{
				return false;
			}
		}

		if (!SMS_RX_TESTING_ACK_MINIMAL && (dataPacketFormat == 0x03U) && responseRequested && (peerId != 0U) && (peerId != trxDMRID))
		{
			// BrandMeister/Motorola control headers (0x43 xx) expect an immediate response-PDU ACK.
			smsScheduleAckResponse(peerId);
		}

		if ((dataPacketFormat == 0x03U) && !fullMessageFlag)
		{
			if (SMS_RX_TESTING_OPEN_DOOR)
			{
				smsStoreInboxMessage(peerId, "RX_OPEN_DOOR_F03_CONTROL");
			}

			// Treat non-full format-0x03 headers as control traffic and keep current assembly intact.
			return true;
		}

		if ((dataPacketFormat == 0x03U) && smsRxAssemblyIsActive())
		{
			bool restartAssembly = false;

			if (fullMessageFlag && (peerId == rxAssembly.sourceId) && (blocks == rxAssembly.expectedBlocks) && (rxAssembly.receivedBlocks > 0U))
			{
				uint32_t now = ticksGetMillis();
				uint32_t elapsed = (smsRxTestingLastStoreTick != 0U) ? (uint32_t)(now - smsRxTestingLastStoreTick) : 0xFFFFFFFFU;

				if (elapsed >= SMS_RX_RETX_GUARD_MS)
				{
					restartAssembly = true;
				}
			}

			if (!restartAssembly)
			{
				if (SMS_RX_TESTING_OPEN_DOOR)
				{
					smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_F03_INTERLEAVE");
				}

				// Do not restart an active data assembly on normal interleaved format-0x03 headers.
				return true;
			}

			if (SMS_RX_TESTING_OPEN_DOOR)
			{
				smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_RETX_RESTART");
			}
		}

		if (SMS_RX_TESTING_OPEN_DOOR && smsRxAssemblyIsActive() && (rxAssembly.receivedBlocks > 0U))
		{
			smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_PARTIAL_RESTART");
		}

		smsRxAssemblySetActive(true);
		rxAssembly.expectedBlocks = blocks;
		rxAssembly.receivedBlocks = 0U;
		rxAssembly.padOctets = pad;
		smsRxAssemblySetFormat03(dataPacketFormat == 0x03U);
		smsRxAssemblySetBlockPayloadBytes(SMS_BLOCK_DATA_BYTES);
		smsRxAssemblySetResponseRequested(responseRequested);
		rxAssembly.sourceId = peerId;
		memset(rxAssembly.payload, 0, sizeof(rxAssembly.payload));
		smsTestingStoreOpenDoorMarker(peerId);
		smsRxTestingLastStoreTick = ticksGetMillis();
		return true;
	}

	if (((dataType == 0x07U) || (dataType == 0x08U) || (dataType == 0x09U)) && smsRxAssemblyIsActive())
	{
		const uint8_t *blockPayload = frame;
		uint8_t blockPayloadBytes = smsRxAssemblyGetBlockPayloadBytes();
		uint16_t blockOffset;

		if (smsRxAssemblyIsFormat03() && (rxAssembly.receivedBlocks > 0U))
		{
			uint8_t inferredPayloadBytes = 0U;
			uint16_t inferredIpPacketLength = 0U;

			if (smsTryInferConfirmedBlockLayout(frame, &inferredPayloadBytes, &inferredIpPacketLength))
			{
				uint8_t requiredBlocks = smsComputeRequiredBlocks(inferredIpPacketLength, inferredPayloadBytes);

				if ((requiredBlocks != 0U) && (requiredBlocks == rxAssembly.expectedBlocks))
				{
					if (SMS_RX_TESTING_OPEN_DOOR)
					{
						smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_RETX_BLK0");
					}

					rxAssembly.receivedBlocks = 0U;
					smsRxAssemblySetBlockPayloadBytes(SMS_BLOCK_DATA_BYTES);
					memset(rxAssembly.payload, 0, sizeof(rxAssembly.payload));
				}
			}
		}

		if (smsRxAssemblyIsFormat03() && (rxAssembly.receivedBlocks == 0U))
		{
			uint8_t inferredPayloadBytes = 0U;
			uint16_t inferredIpPacketLength = 0U;

			if (smsTryInferConfirmedBlockLayout(frame, &inferredPayloadBytes, &inferredIpPacketLength))
			{
				blockPayloadBytes = inferredPayloadBytes;
				smsRxAssemblySetBlockPayloadBytes(inferredPayloadBytes);

				if ((inferredIpPacketLength >= 20U) && (inferredPayloadBytes > 0U))
				{
					uint8_t requiredBlocks = smsComputeRequiredBlocks(inferredIpPacketLength, inferredPayloadBytes);

					if ((requiredBlocks >= rxAssembly.expectedBlocks) && (requiredBlocks <= SMS_MAX_RX_DATA_BLOCKS))
					{
						rxAssembly.expectedBlocks = requiredBlocks;
					}
				}
			}
		}

		if (blockPayloadBytes < SMS_BLOCK_DATA_BYTES)
		{
			// Confirmed block layout: frame starts with a 2-byte block prefix, payload follows.
			blockPayload = &frame[SMS_BLOCK_DATA_BYTES - blockPayloadBytes];
		}

		if ((blockPayloadBytes == 0U) || (rxAssembly.receivedBlocks >= rxAssembly.expectedBlocks))
		{
			smsResetRxAssembly();
			return false;
		}

		blockOffset = (uint16_t)(rxAssembly.receivedBlocks * smsRxAssemblyGetBlockPayloadBytes());
		if ((uint16_t)(blockOffset + blockPayloadBytes) > sizeof(rxAssembly.payload))
		{
			smsResetRxAssembly();
			return false;
		}

		memcpy(&rxAssembly.payload[blockOffset], blockPayload, blockPayloadBytes);
		rxAssembly.receivedBlocks++;
		smsRxTestingLastStoreTick = ticksGetMillis();

		if (SMS_RX_TESTING_OPEN_DOOR && ((dataType == 0x08U) || (dataType == 0x09U)))
		{
			smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_ALT_BLOCKTYPE");
		}

		if (SMS_RX_TESTING_OPEN_DOOR && (rxAssembly.receivedBlocks == 1U))
		{
			smsStoreInboxMessage(rxAssembly.sourceId, "RX_OPEN_DOOR_H07_FIRST");
		}

		if (rxAssembly.receivedBlocks >= rxAssembly.expectedBlocks)
		{
			uint8_t assemblyBlockPayloadBytes = smsRxAssemblyGetBlockPayloadBytes();
			uint16_t totalLength = (uint16_t)(rxAssembly.expectedBlocks * assemblyBlockPayloadBytes);
			uint8_t effectivePadOctets = rxAssembly.padOctets;
			uint16_t payloadLength;
			uint16_t standardTextOffsetLength = 0U;
			uint16_t standardTextOffsetLengthUtf16 = 0U;
			uint16_t motorolaTextOffsetLength = 0U;
			uint16_t motorolaTextOffsetLengthUtf16 = 0U;
			bool hasMotorolaSignature = false;
			bool hasMotorolaPort = false;
			bool hasStandardPort = false;
			char decodedText[SMS_MAX_RX_TEXT_LENGTH + 1U] = { 0 };
			bool decoded = false;

			if (!smsRxAssemblyIsFormat03() &&
				(effectivePadOctets >= assemblyBlockPayloadBytes) &&
				(totalLength > SMS_STANDARD_CRC32_BYTES) &&
				(totalLength >= 4U))
			{
				uint16_t ipPacketLength = (uint16_t)(((uint16_t)rxAssembly.payload[2] << 8) | rxAssembly.payload[3]);
				if ((ipPacketLength >= 20U) && (ipPacketLength <= (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES)))
				{
					uint16_t inferredPadOctets = (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES - ipPacketLength);
					if (inferredPadOctets < assemblyBlockPayloadBytes)
					{
						effectivePadOctets = (uint8_t)inferredPadOctets;
					}
				}
			}

			if (smsRxAssemblyIsFormat03() || (effectivePadOctets >= assemblyBlockPayloadBytes))
			{
				effectivePadOctets = 0U;
			}

			payloadLength = (uint16_t)(totalLength - effectivePadOctets);

			if (payloadLength >= 24U)
			{
				uint16_t srcPort = (uint16_t)(((uint16_t)rxAssembly.payload[20] << 8) | rxAssembly.payload[21]);
				uint16_t dstPort = (uint16_t)(((uint16_t)rxAssembly.payload[22] << 8) | rxAssembly.payload[23]);
				hasMotorolaPort = ((srcPort == SMS_MOTOROLA_UDP_PORT) || (dstPort == SMS_MOTOROLA_UDP_PORT));
				hasStandardPort = ((srcPort == SMS_STANDARD_UDP_PORT) || (dstPort == SMS_STANDARD_UDP_PORT));
			}

			if (payloadLength > SMS_STANDARD_TEXT_OFFSET)
			{
				standardTextOffsetLength = (uint16_t)(payloadLength - SMS_STANDARD_TEXT_OFFSET);
				if (standardTextOffsetLength > SMS_MAX_RX_UTF16_PAYLOAD_BYTES)
				{
					standardTextOffsetLength = SMS_MAX_RX_UTF16_PAYLOAD_BYTES;
				}
				standardTextOffsetLengthUtf16 = (uint16_t)(standardTextOffsetLength & 0xFFFEU);
			}

			if (payloadLength > SMS_MOTOROLA_TEXT_OFFSET)
			{
				motorolaTextOffsetLength = (uint16_t)(payloadLength - SMS_MOTOROLA_TEXT_OFFSET);
				if (motorolaTextOffsetLength > SMS_MAX_RX_UTF16_PAYLOAD_BYTES)
				{
					motorolaTextOffsetLength = SMS_MAX_RX_UTF16_PAYLOAD_BYTES;
				}
				motorolaTextOffsetLengthUtf16 = (uint16_t)(motorolaTextOffsetLength & 0xFFFEU);

				hasMotorolaSignature =
					(rxAssembly.payload[28] == 0x00U) &&
					((rxAssembly.payload[30] == 0xA0U) || (rxAssembly.payload[30] == 0xE0U)) &&
					(rxAssembly.payload[31] == 0x00U) &&
					(rxAssembly.payload[33] == 0x04U) &&
					(rxAssembly.payload[34] == 0x0DU) &&
					(rxAssembly.payload[35] == 0x00U) &&
					(rxAssembly.payload[36] == 0x0AU) &&
					(rxAssembly.payload[37] == 0x00U);
			}

			if ((totalLength > 0U) && (totalLength <= sizeof(rxAssembly.payload)))
			{
				uint16_t udpPayloadOffset = 28U;
				uint16_t udpPayloadLength = 0U;
				uint16_t ipPacketLength = 0U;

				if (payloadLength >= 4U)
				{
					ipPacketLength = (uint16_t)(((uint16_t)rxAssembly.payload[2] << 8) | rxAssembly.payload[3]);
				}

				if ((ipPacketLength >= udpPayloadOffset) && (ipPacketLength <= payloadLength))
				{
					udpPayloadLength = (uint16_t)(ipPacketLength - udpPayloadOffset);
				}
				else if (payloadLength > udpPayloadOffset)
				{
					udpPayloadLength = (uint16_t)(payloadLength - udpPayloadOffset);
				}

				decoded = smsDecodeMotorolaPayload(rxAssembly.payload, totalLength, effectivePadOctets, decodedText);

				if (!decoded)
				{
					decoded = smsDecodeStandardPayload(rxAssembly.payload, totalLength, effectivePadOctets, decodedText);
				}

				if (!decoded && (hasMotorolaSignature || hasMotorolaPort) && (motorolaTextOffsetLengthUtf16 >= 2U))
				{
					decoded = smsDecodeUtf16Payload(&rxAssembly.payload[SMS_MOTOROLA_TEXT_OFFSET], motorolaTextOffsetLengthUtf16, decodedText);
				}

				if (!decoded && hasStandardPort && (standardTextOffsetLengthUtf16 >= 2U))
				{
					decoded = smsDecodeUtf16Payload(&rxAssembly.payload[SMS_STANDARD_TEXT_OFFSET], standardTextOffsetLengthUtf16, decodedText);
				}

				if (!decoded && (hasMotorolaSignature || hasMotorolaPort) && (motorolaTextOffsetLength >= 1U))
				{
					decoded = smsDecodeAsciiPayload(&rxAssembly.payload[SMS_MOTOROLA_TEXT_OFFSET], motorolaTextOffsetLength, decodedText);
				}

				if (!decoded && hasStandardPort && (standardTextOffsetLength >= 1U))
				{
					decoded = smsDecodeAsciiPayload(&rxAssembly.payload[SMS_STANDARD_TEXT_OFFSET], standardTextOffsetLength, decodedText);
				}

				if (!decoded && (hasMotorolaPort || hasStandardPort) && (udpPayloadLength >= 4U))
				{
					decoded = smsDecodeUtf16PayloadSliding(&rxAssembly.payload[udpPayloadOffset], udpPayloadLength, decodedText);
				}

				if (!decoded && (hasMotorolaPort || hasStandardPort) && (udpPayloadLength >= 3U))
				{
					decoded = smsDecodeAsciiPayloadSliding(&rxAssembly.payload[udpPayloadOffset], udpPayloadLength, decodedText);
				}

				if (!decoded && smsRxAssemblyIsFormat03())
				{
					decoded = smsDecodeIpUdpPayloadWithoutCrc(rxAssembly.payload, payloadLength, decodedText);
				}

				if (!decoded && (hasMotorolaPort || hasStandardPort) && (udpPayloadLength >= 6U) && SMS_RX_TESTING_OPEN_DOOR)
				{
					decoded = smsForceDecodePayload(&rxAssembly.payload[udpPayloadOffset], udpPayloadLength, decodedText);
				}

				if (!decoded && (payloadLength >= 12U) && SMS_RX_TESTING_OPEN_DOOR)
				{
					decoded = smsForceDecodePayload(rxAssembly.payload, payloadLength, decodedText);
				}

				if (!decoded && SMS_RX_TESTING_OPEN_DOOR)
				{
					strncpy(decodedText, "RX_OPEN_DOOR_FRAME", SMS_MAX_RX_TEXT_LENGTH);
					decodedText[SMS_MAX_RX_TEXT_LENGTH] = 0;
					decoded = true;
				}
			}

			if (decoded)
			{
				smsStoreInboxMessage(rxAssembly.sourceId, decodedText);
			}

			// Some hotspots send additional segments only after ACK, even when this packet is not text-decodable.
			if (smsRxAssemblyIsResponseRequested() && (rxAssembly.sourceId != trxDMRID))
			{
				smsScheduleAckResponse(rxAssembly.sourceId);
			}

			smsResetRxAssembly();
		}

		return true;
	}

	return false;
}

uint8_t smsGetInboxCount(void)
{
	return inboxCount;
}

bool smsGetInboxMessage(uint8_t index, smsInboxMessage_t *message)
{
	uint8_t absoluteIndex;

	if ((message == NULL) || (index >= inboxCount))
	{
		return false;
	}

	absoluteIndex = (uint8_t)((inboxStart + index) % SMS_INBOX_MAX_MESSAGES);
	*message = inboxMessages[absoluteIndex];
	return true;
}

bool smsDeleteInboxMessage(uint8_t index)
{
	uint8_t lastIndex;

	if (index >= inboxCount)
	{
		return false;
	}

	for (uint8_t i = index; i < (uint8_t)(inboxCount - 1U); i++)
	{
		uint8_t from = (uint8_t)((inboxStart + i + 1U) % SMS_INBOX_MAX_MESSAGES);
		uint8_t to = (uint8_t)((inboxStart + i) % SMS_INBOX_MAX_MESSAGES);
		inboxMessages[to] = inboxMessages[from];
	}

	lastIndex = (uint8_t)((inboxStart + inboxCount - 1U) % SMS_INBOX_MAX_MESSAGES);
	memset(&inboxMessages[lastIndex], 0, sizeof(smsInboxMessage_t));
	inboxCount--;

	if (inboxCount == 0U)
	{
		inboxStart = 0U;
	}

	smsStorageMarkDirty();

	return true;
}

void smsClearInbox(void)
{
	inboxStart = 0U;
	inboxCount = 0U;
	memset(inboxMessages, 0, sizeof(inboxMessages));
	smsResetRxAssembly();
	smsStorageMarkDirty();
}

uint8_t smsGetSentCount(void)
{
	return sentCount;
}

bool smsGetSentMessage(uint8_t index, smsSentMessage_t *message)
{
	uint8_t absoluteIndex;

	if ((message == NULL) || (index >= sentCount))
	{
		return false;
	}

	absoluteIndex = (uint8_t)((sentStart + index) % SMS_SENT_MAX_MESSAGES);
	*message = sentMessages[absoluteIndex];
	return true;
}

bool smsStoreSentMessage(uint32_t destinationId, const char *text)
{
	if ((text == NULL) || (text[0] == 0) || (destinationId == 0U) || (destinationId > 0x00FFFFFFU))
	{
		return false;
	}

	smsStoreSentMessageInternal(destinationId, text);
	return true;
}

bool smsDeleteSentMessage(uint8_t index)
{
	uint8_t lastIndex;

	if (index >= sentCount)
	{
		return false;
	}

	for (uint8_t i = index; i < (uint8_t)(sentCount - 1U); i++)
	{
		uint8_t from = (uint8_t)((sentStart + i + 1U) % SMS_SENT_MAX_MESSAGES);
		uint8_t to = (uint8_t)((sentStart + i) % SMS_SENT_MAX_MESSAGES);
		sentMessages[to] = sentMessages[from];
	}

	lastIndex = (uint8_t)((sentStart + sentCount - 1U) % SMS_SENT_MAX_MESSAGES);
	memset(&sentMessages[lastIndex], 0, sizeof(smsSentMessage_t));
	sentCount--;

	if (sentCount == 0U)
	{
		sentStart = 0U;
	}

	smsStorageMarkDirty();

	return true;
}

void smsClearSent(void)
{
	sentStart = 0U;
	sentCount = 0U;
	memset(sentMessages, 0, sizeof(sentMessages));
	smsStorageMarkDirty();
}

uint8_t smsGetQuickTextCount(void)
{
	return quickTextCount;
}

bool smsGetQuickTextMessage(uint8_t index, smsQuickTextMessage_t *message)
{
	smsStorage_t storage;

	if ((message == NULL) || (index >= quickTextCount))
	{
		return false;
	}

	if (!EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)&storage, (int)sizeof(storage)) ||
		(storage.magic != SMS_STORAGE_MAGIC) ||
		(storage.version != SMS_STORAGE_VERSION) ||
		(storage.quickTextCount > SMS_QUICKTEXT_MAX_MESSAGES) ||
		(storage.checksum != smsStorageChecksum(&storage)) ||
		(index >= storage.quickTextCount))
	{
		return false;
	}

	*message = storage.quickTextMessages[index];
	message->title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
	message->text[SMS_MAX_TX_TEXT_LENGTH] = 0;
	return true;
}

bool smsStoreQuickTextMessage(const char *title, const char *text)
{
	smsStorage_t storage;

	if ((title == NULL) || (title[0] == 0) || (text == NULL) || (text[0] == 0) || (quickTextCount >= SMS_QUICKTEXT_MAX_MESSAGES))
	{
		return false;
	}

	smsStorageBuildSnapshot(&storage);
	if (storage.quickTextCount >= SMS_QUICKTEXT_MAX_MESSAGES)
	{
		return false;
	}

	strncpy(storage.quickTextMessages[storage.quickTextCount].title, title, SMS_QUICKTEXT_MAX_TITLE_LENGTH);
	storage.quickTextMessages[storage.quickTextCount].title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
	strncpy(storage.quickTextMessages[storage.quickTextCount].text, text, SMS_MAX_TX_TEXT_LENGTH);
	storage.quickTextMessages[storage.quickTextCount].text[SMS_MAX_TX_TEXT_LENGTH] = 0;
	storage.quickTextCount++;
	quickTextCount = (uint8_t)storage.quickTextCount;
	storage.checksum = smsStorageChecksum(&storage);
	return EEPROM_Write(SMS_STORAGE_ADDRESS, (uint8_t *)&storage, (int)sizeof(storage));
}

bool smsUpdateQuickTextMessage(uint8_t index, const char *title, const char *text)
{
	smsStorage_t storage;

	if ((index >= quickTextCount) || (title == NULL) || (title[0] == 0) || (text == NULL) || (text[0] == 0))
	{
		return false;
	}

	smsStorageBuildSnapshot(&storage);
	if (index >= storage.quickTextCount)
	{
		return false;
	}

	strncpy(storage.quickTextMessages[index].title, title, SMS_QUICKTEXT_MAX_TITLE_LENGTH);
	storage.quickTextMessages[index].title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
	strncpy(storage.quickTextMessages[index].text, text, SMS_MAX_TX_TEXT_LENGTH);
	storage.quickTextMessages[index].text[SMS_MAX_TX_TEXT_LENGTH] = 0;
	storage.checksum = smsStorageChecksum(&storage);
	return EEPROM_Write(SMS_STORAGE_ADDRESS, (uint8_t *)&storage, (int)sizeof(storage));
}

bool smsDeleteQuickTextMessage(uint8_t index)
{
	smsStorage_t storage;

	if (index >= quickTextCount)
	{
		return false;
	}

	smsStorageBuildSnapshot(&storage);
	if (index >= storage.quickTextCount)
	{
		return false;
	}

	for (uint8_t i = index; i < (uint8_t)(storage.quickTextCount - 1U); i++)
	{
		storage.quickTextMessages[i] = storage.quickTextMessages[i + 1U];
	}

	memset(&storage.quickTextMessages[storage.quickTextCount - 1U], 0, sizeof(smsQuickTextMessage_t));
	storage.quickTextCount--;
	quickTextCount = (uint8_t)storage.quickTextCount;
	storage.checksum = smsStorageChecksum(&storage);
	return EEPROM_Write(SMS_STORAGE_ADDRESS, (uint8_t *)&storage, (int)sizeof(storage));
}

smsPackResult_t smsQueueSentMessage(uint8_t index, uint32_t sourceId)
{
	smsSentMessage_t message;

	if (smsGetSentMessage(index, &message) == false)
	{
		return SMS_PACK_ERROR_INVALID_INDEX;
	}

	return smsQueueMessage(message.destinationId, sourceId, message.text);
}

bool smsHasRxNotification(void)
{
	return inboxUnreadNotification;
}

bool smsConsumeRxNotification(void)
{
	bool pending = inboxUnreadNotification;
	inboxUnreadNotification = false;
	return pending;
}

void smsInboxStorageTick(void)
{
	uint32_t now;

	if (!smsStorageDirty)
	{
		smsStorageDirtySinceTick = 0U;
		return;
	}

	now = ticksGetMillis();

	if (smsStorageDirtySinceTick == 0U)
	{
		smsStorageDirtySinceTick = now;
		return;
	}

	if ((now - smsStorageDirtySinceTick) < SMS_STORAGE_DEBOUNCE_MS)
	{
		return;
	}

	if (smsStoragePersist())
	{
		smsStorageDirty = false;
		smsStorageDirtySinceTick = 0U;
	}
}
