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

#define SMS_STORAGE_ADDRESS                    0x0F0000
#define SMS_STORAGE_MAGIC                      0x534D5349U
#define SMS_STORAGE_VERSION                    5U
#define SMS_LEGACY_TEXT_LENGTH                 64U
#define SMS_STORAGE_DEBOUNCE_MS                1500U
#define SMS_TX_ACK_TIMEOUT_MS                  6000U
#define SMS_MOTOROLA_UDP_PORT                0x0FA7U
#define SMS_MOTOROLA_IPV4_PROTOCOL           0x11U
#define SMS_MOTOROLA_IPV4_TTL                0x01U
#define SMS_MOTOROLA_TEXT_OFFSET               38U
#define SMS_MOTOROLA_INTERNAL_HEADER_SIZE      10U
#define SMS_STANDARD_TEXT_OFFSET               32U
#define SMS_STANDARD_UDP_PORT                0x1398U
#define SMS_RX_MAX_PAYLOAD_BYTES      (SMS_MAX_RX_DATA_BLOCKS * SMS_BLOCK_DATA_BYTES)
#define SMS_RX_MIN_UTF16_LE_RUN_CHARS           6U
#define SMS_WINDOW_NEIGHBORHOOD_RADIUS         12U
#define SMS_WINDOW_NEIGHBORHOOD_STEP            2U
#define SMS_ACK_PROFILE_LEGACY                 0U
#define SMS_ACK_PROFILE_MODERN                 1U

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
	uint32_t inboxCount;
	uint32_t sentCount;
	uint32_t quickTextCount;
	uint32_t checksum;
} smsStorageHeader_t;

typedef struct
{
	uint32_t sourceId;
	char text[SMS_LEGACY_TEXT_LENGTH + 1U];
} smsLegacyInboxMessage_t;

typedef struct
{
	uint32_t destinationId;
	char text[SMS_LEGACY_TEXT_LENGTH + 1U];
} smsLegacySentMessage_t;

typedef struct
{
	char title[SMS_QUICKTEXT_MAX_TITLE_LENGTH + 1U];
	char text[SMS_LEGACY_TEXT_LENGTH + 1U];
} smsLegacyQuickTextMessage_t;

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t inboxCount;
	uint32_t sentCount;
	uint32_t quickTextCount;
	uint32_t checksum;
	smsLegacyInboxMessage_t inboxMessages[SMS_INBOX_MAX_MESSAGES];
	smsLegacySentMessage_t sentMessages[SMS_SENT_MAX_MESSAGES];
	smsLegacyQuickTextMessage_t quickTextMessages[SMS_QUICKTEXT_MAX_MESSAGES];
} smsLegacyStorageV4_t;

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t messageCount;
	uint32_t checksum;
	smsLegacyInboxMessage_t messages[SMS_INBOX_MAX_MESSAGES];
} smsLegacyInboxStorageV1_t;

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t messageCount;
	uint32_t checksum;
} smsLegacyInboxStorageV1Header_t;

static smsPreparedMessage_t queuedMessage;
static bool queuedMessageValid = false;

typedef struct
{
	bool active;
	uint8_t expectedBlocks;
	uint8_t receivedBlocks;
	uint16_t payloadBytesReceived;
	uint8_t padOctets;
	bool responseRequested;
	uint8_t ackProfile;
	uint32_t sourceId;
} smsRxAssembly_t;

typedef struct
{
	bool active;
	uint16_t totalLength;
	uint8_t padOctets;
	bool responseRequested;
	uint8_t ackProfile;
	uint32_t sourceId;
} smsRxDecodePending_t;

typedef struct
{
	bool active;
	uint32_t destinationId;
	uint8_t ackProfile;
} smsAckResponseTracking_t;

typedef struct
{
	bool active;
	uint32_t destinationId;
	uint32_t sourceId;
	ticksTimer_t ackTimer;
	char text[SMS_MAX_TEXT_LENGTH + 1U];
} smsOutgoingTracking_t;

static uint8_t inboxCount = 0U;
static uint16_t smsIpSequenceNumber = 0U;
static uint8_t sentCount = 0U;
static uint8_t quickTextCount = 0U;
static bool inboxUnreadNotification = false;
static __attribute__((section(".ccmram"))) uint8_t smsRxPayloadBuffer[SMS_RX_MAX_PAYLOAD_BYTES];
static __attribute__((section(".ccmram"))) uint8_t smsRxDecodePendingPayloadBuffer[SMS_RX_MAX_PAYLOAD_BYTES];
enum
{
	SMS_DECODE_BUF_MAIN = 0,
	SMS_DECODE_BUF_SCRATCH,
	SMS_DECODE_BUF_COUNT
};
static __attribute__((section(".ccmram"))) char smsDecodeTextBuffers[SMS_DECODE_BUF_COUNT][SMS_MAX_TEXT_LENGTH + 1U];
static smsRxAssembly_t rxAssembly = { 0 };
static volatile smsRxDecodePending_t rxDecodePending = { 0 };
static smsAckResponseTracking_t ackResponseTracking = { 0 };
static smsOutgoingTracking_t outgoingTracking = { 0 };
static smsTxEvent_t pendingTxEvent = SMS_TX_EVENT_NONE;
static volatile bool smsStorageDirty = false;
static uint32_t smsStorageDirtySinceTick = 0U;

static void smsResetRxAssembly(void);
static void smsScheduleAckResponse(uint32_t destinationId, uint8_t ackProfile);
static bool smsQueueAckResponseMessage(uint32_t destinationId, uint32_t sourceId, uint8_t ackProfile);
static uint16_t smsCrc16Ccitt(const uint8_t *data, uint8_t length);
static void smsResetOutgoingTracking(void);
static void smsSetPendingTxEvent(smsTxEvent_t event);
static smsPackResult_t smsConvertTextToUtf16LeUpper(const char *text, uint8_t *payload, uint16_t *payloadLength);
static smsPackResult_t smsBuildMotorolaPayload(uint32_t destinationId, uint32_t sourceId, const char *text, uint8_t *payload, uint16_t *payloadLength, uint8_t *padOctetCount);
static bool smsDecodeMotorolaPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, char *textOut);
static bool smsDecodeStandardPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, char *textOut);
static bool smsDecodeUtf8Payload(const uint8_t *payload, uint16_t payloadLength, char *textOut);
static bool smsDecodeUtf16LeRun(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut);
static bool smsDecodeUtf16BeRun(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut);
static bool smsDecodeUtf16PayloadByScan(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut);
static bool smsDecodeUtf8PayloadByScan(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut);
static uint16_t smsCountCharOccurrence(const char *text, char value);
static int32_t smsScoreDecodedText(const char *text);
static bool smsIsBetterDecodedCandidate(const char *currentText, const char *candidateText);
static bool smsMergeDecodedCandidate(char *decodedText, const char *candidateText);
static bool smsTryAdoptDecodedCandidate(char *decodedText, const char *candidateText);
static bool smsTryDecodeWindowDirect(const uint8_t *payload, uint16_t payloadLength, bool allowAscii, char *decodedText, char *scratchText);
static void smsTryDecodeWindowNeighborhood(const uint8_t *payload, uint16_t payloadLength, uint16_t centerOffset, uint16_t radius, uint16_t step, bool allowAscii, char *decodedText, char *scratchText);
static char smsMapUnicodeToAscii(uint32_t codePoint);
static uint32_t smsStorageChecksum(const smsStorage_t *storage);
static bool smsStorageChecksumFromEeprom(uint32_t startAddress, uint32_t totalBytes, uint32_t checksumOffset, uint32_t *checksumOut);
static bool smsStorageReadHeader(smsStorageHeader_t *header);
static bool smsStorageWriteHeader(const smsStorageHeader_t *header);
static bool smsStorageUpdateChecksum(void);
static bool smsStorageReadCurrent(smsStorage_t *storage);
static bool smsStorageWriteCurrent(smsStorage_t *storage);
static void smsStorageBuildSnapshot(smsStorage_t *storage);
static bool smsStoragePersist(void);
static void smsStorageLoad(void);
static bool smsShouldAckUndecodedPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets);
static bool smsDecodeCurrentRxBuffers(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, uint32_t sourceId);
static void smsProcessPendingRxDecode(void);

void smsInit(void)
{
	memset(&queuedMessage, 0, sizeof(queuedMessage));
	queuedMessageValid = false;
	inboxCount = 0U;
	sentCount = 0U;
	quickTextCount = 0U;
	inboxUnreadNotification = false;
	smsResetRxAssembly();
	memset(smsRxDecodePendingPayloadBuffer, 0, sizeof(smsRxDecodePendingPayloadBuffer));
	memset((void *)&rxDecodePending, 0, sizeof(rxDecodePending));
	smsResetOutgoingTracking();
	pendingTxEvent = SMS_TX_EVENT_NONE;
	smsStorageDirty = false;
	smsStorageDirtySinceTick = 0U;
	smsStorageLoad();
}

static void smsResetOutgoingTracking(void)
{
	memset(&outgoingTracking, 0, sizeof(outgoingTracking));
}

static void smsSetPendingTxEvent(smsTxEvent_t event)
{
	pendingTxEvent = event;
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

static bool smsStorageChecksumFromEeprom(uint32_t startAddress, uint32_t totalBytes, uint32_t checksumOffset, uint32_t *checksumOut)
{
	uint8_t buffer[64U];
	uint32_t offset = 0U;
	uint32_t checksum = 2166136261UL;

	if (checksumOut == NULL)
	{
		return false;
	}

	while (offset < totalBytes)
	{
		uint32_t remaining = (uint32_t)(totalBytes - offset);
		uint32_t chunk = (remaining > sizeof(buffer)) ? (uint32_t)sizeof(buffer) : remaining;

		if (!EEPROM_Read((int32_t)(startAddress + offset), buffer, (int)chunk))
		{
			return false;
		}

		for (uint32_t i = 0U; i < chunk; i++)
		{
			uint32_t absoluteOffset = offset + i;

			if ((absoluteOffset >= checksumOffset) && (absoluteOffset < (checksumOffset + sizeof(uint32_t))))
			{
				continue;
			}

			checksum ^= buffer[i];
			checksum *= 16777619UL;
		}

		offset += chunk;
	}

	*checksumOut = checksum;

	return true;
}

static bool smsStorageReadHeader(smsStorageHeader_t *header)
{
	if (header == NULL)
	{
		return false;
	}

	if (!EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)header, (int)sizeof(*header)))
	{
		return false;
	}

	if ((header->magic != SMS_STORAGE_MAGIC) ||
		(header->version != SMS_STORAGE_VERSION) ||
		(header->inboxCount > SMS_INBOX_MAX_MESSAGES) ||
		(header->sentCount > SMS_SENT_MAX_MESSAGES) ||
		(header->quickTextCount > SMS_QUICKTEXT_MAX_MESSAGES))
	{
		return false;
	}

	return true;
}

static bool smsStorageReadCurrent(smsStorage_t *storage)
{
	if (storage == NULL)
	{
		return false;
	}

	if (EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)storage, (int)sizeof(*storage)) &&
		(storage->magic == SMS_STORAGE_MAGIC) &&
		(storage->version == SMS_STORAGE_VERSION) &&
		(storage->inboxCount <= SMS_INBOX_MAX_MESSAGES) &&
		(storage->sentCount <= SMS_SENT_MAX_MESSAGES) &&
		(storage->quickTextCount <= SMS_QUICKTEXT_MAX_MESSAGES) &&
		(storage->checksum == smsStorageChecksum(storage)))
	{
		for (uint8_t i = 0U; i < SMS_INBOX_MAX_MESSAGES; i++)
		{
			storage->inboxMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
		}

		for (uint8_t i = 0U; i < SMS_SENT_MAX_MESSAGES; i++)
		{
			storage->sentMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
		}

		for (uint8_t i = 0U; i < SMS_QUICKTEXT_MAX_MESSAGES; i++)
		{
			storage->quickTextMessages[i].title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
			storage->quickTextMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
		}

		return true;
	}

	memset(storage, 0, sizeof(*storage));
	storage->magic = SMS_STORAGE_MAGIC;
	storage->version = SMS_STORAGE_VERSION;
	return false;
}

static bool smsStorageWriteHeader(const smsStorageHeader_t *header)
{
	if (header == NULL)
	{
		return false;
	}

	return EEPROM_Write(SMS_STORAGE_ADDRESS, (uint8_t *)header, (int)sizeof(*header));
}

static bool smsStorageUpdateChecksum(void)
{
	uint8_t chunk[64];
	uint32_t checksum = 2166136261UL;
	uint32_t offset = 0U;
	const uint32_t checksumOffset = (uint32_t)offsetof(smsStorage_t, checksum);
	const uint32_t checksumEnd = (uint32_t)(checksumOffset + sizeof(uint32_t));

	while (offset < sizeof(smsStorage_t))
	{
		uint32_t chunkLength = (uint32_t)sizeof(chunk);
		if ((offset + chunkLength) > sizeof(smsStorage_t))
		{
			chunkLength = (uint32_t)(sizeof(smsStorage_t) - offset);
		}

		if (!EEPROM_Read((int32_t)(SMS_STORAGE_ADDRESS + offset), chunk, (int)chunkLength))
		{
			return false;
		}

		for (uint32_t i = 0U; i < chunkLength; i++)
		{
			uint32_t absoluteIndex = (uint32_t)(offset + i);
			if ((absoluteIndex >= checksumOffset) && (absoluteIndex < checksumEnd))
			{
				continue;
			}

			checksum ^= chunk[i];
			checksum *= 16777619UL;
		}

		offset = (uint32_t)(offset + chunkLength);
	}

	return EEPROM_Write((int32_t)(SMS_STORAGE_ADDRESS + checksumOffset), (uint8_t *)&checksum, (int)sizeof(checksum));
}

static bool smsStorageWriteCurrent(smsStorage_t *storage)
{
	if (storage == NULL)
	{
		return false;
	}

	if (storage->inboxCount > SMS_INBOX_MAX_MESSAGES)
	{
		storage->inboxCount = SMS_INBOX_MAX_MESSAGES;
	}

	if (storage->sentCount > SMS_SENT_MAX_MESSAGES)
	{
		storage->sentCount = SMS_SENT_MAX_MESSAGES;
	}

	if (storage->quickTextCount > SMS_QUICKTEXT_MAX_MESSAGES)
	{
		storage->quickTextCount = SMS_QUICKTEXT_MAX_MESSAGES;
	}

	storage->magic = SMS_STORAGE_MAGIC;
	storage->version = SMS_STORAGE_VERSION;

	for (uint8_t i = 0U; i < SMS_INBOX_MAX_MESSAGES; i++)
	{
		storage->inboxMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
	}

	for (uint8_t i = 0U; i < SMS_SENT_MAX_MESSAGES; i++)
	{
		storage->sentMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
	}

	for (uint8_t i = 0U; i < SMS_QUICKTEXT_MAX_MESSAGES; i++)
	{
		storage->quickTextMessages[i].title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
		storage->quickTextMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
	}

	storage->checksum = smsStorageChecksum(storage);
	return EEPROM_Write(SMS_STORAGE_ADDRESS, (uint8_t *)storage, (int)sizeof(*storage));
}

static void smsStorageBuildSnapshot(smsStorage_t *storage)
{
	bool existingStorageValid = false;

	if (storage == NULL)
	{
		return;
	}

	existingStorageValid = smsStorageReadCurrent(storage);

	storage->magic = SMS_STORAGE_MAGIC;
	storage->version = SMS_STORAGE_VERSION;
	storage->inboxCount = inboxCount;
	storage->sentCount = sentCount;
	memset(storage->inboxMessages, 0, sizeof(storage->inboxMessages));
	memset(storage->sentMessages, 0, sizeof(storage->sentMessages));

	for (uint8_t i = 0U; i < inboxCount; i++)
	{
		smsInboxMessage_t message;

		if (smsGetInboxMessage(i, &message))
		{
			storage->inboxMessages[i] = message;
			storage->inboxMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
		}
	}

	for (uint8_t i = 0U; i < sentCount; i++)
	{
		smsSentMessage_t message;

		if (smsGetSentMessage(i, &message))
		{
			storage->sentMessages[i] = message;
			storage->sentMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
		}
	}

	if (existingStorageValid)
	{
		if (storage->quickTextCount > SMS_QUICKTEXT_MAX_MESSAGES)
		{
			storage->quickTextCount = SMS_QUICKTEXT_MAX_MESSAGES;
		}

		for (uint8_t i = 0U; i < storage->quickTextCount; i++)
		{
			storage->quickTextMessages[i].title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
			storage->quickTextMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
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
	return smsStorageWriteCurrent(&storage);
}

static void smsStorageLoad(void)
{
	smsStorageHeader_t header;
	uint32_t checksum = 0U;

	if (EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)&header, (int)sizeof(header)) &&
		(header.magic == SMS_STORAGE_MAGIC) &&
		(header.version == SMS_STORAGE_VERSION) &&
		(header.inboxCount <= SMS_INBOX_MAX_MESSAGES) &&
		(header.sentCount <= SMS_SENT_MAX_MESSAGES) &&
		(header.quickTextCount <= SMS_QUICKTEXT_MAX_MESSAGES) &&
		smsStorageChecksumFromEeprom(SMS_STORAGE_ADDRESS,
			(uint32_t)sizeof(smsStorage_t),
			(uint32_t)offsetof(smsStorage_t, checksum),
			&checksum) &&
		(header.checksum == checksum))
	{
		inboxCount = (uint8_t)header.inboxCount;
		sentCount = (uint8_t)header.sentCount;
		quickTextCount = (uint8_t)header.quickTextCount;
		inboxUnreadNotification = false;
		return;
	}

	if (EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)&header, (int)sizeof(header)) &&
		(header.magic == SMS_STORAGE_MAGIC) &&
		(header.version == 4U) &&
		(header.inboxCount <= SMS_INBOX_MAX_MESSAGES) &&
		(header.sentCount <= SMS_SENT_MAX_MESSAGES) &&
		(header.quickTextCount <= SMS_QUICKTEXT_MAX_MESSAGES) &&
		smsStorageChecksumFromEeprom(SMS_STORAGE_ADDRESS,
			(uint32_t)sizeof(smsLegacyStorageV4_t),
			(uint32_t)offsetof(smsLegacyStorageV4_t, checksum),
			&checksum) &&
		(header.checksum == checksum))
	{
		smsStorageHeader_t migratedHeader;
		smsLegacyInboxMessage_t legacyInboxMessage;
		smsLegacySentMessage_t legacySentMessage;
		smsLegacyQuickTextMessage_t legacyQuickTextMessage;
		smsInboxMessage_t inboxMessage;
		smsSentMessage_t sentMessage;
		smsQuickTextMessage_t quickTextMessage;
		bool migrationOk;
		uint8_t migratedInboxCount = (uint8_t)header.inboxCount;
		uint8_t migratedSentCount = (uint8_t)header.sentCount;
		uint8_t migratedQuickTextCount = (uint8_t)header.quickTextCount;

		memset(&migratedHeader, 0, sizeof(migratedHeader));
		migratedHeader.magic = SMS_STORAGE_MAGIC;
		migratedHeader.version = SMS_STORAGE_VERSION;
		migratedHeader.inboxCount = migratedInboxCount;
		migratedHeader.sentCount = migratedSentCount;
		migratedHeader.quickTextCount = migratedQuickTextCount;

		migrationOk = smsStorageWriteHeader(&migratedHeader);

		for (uint8_t i = 0U; (i < migratedInboxCount) && migrationOk; i++)
		{
			uint32_t legacyAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsLegacyStorageV4_t, inboxMessages) + ((uint32_t)i * sizeof(smsLegacyInboxMessage_t)));
			uint32_t targetAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, inboxMessages) + ((uint32_t)i * sizeof(smsInboxMessage_t)));

			migrationOk = EEPROM_Read((int32_t)legacyAddress, (uint8_t *)&legacyInboxMessage, (int)sizeof(legacyInboxMessage));
			if (!migrationOk)
			{
				break;
			}

			memset(&inboxMessage, 0, sizeof(inboxMessage));
			inboxMessage.sourceId = legacyInboxMessage.sourceId;
			strncpy(inboxMessage.text, legacyInboxMessage.text, SMS_MAX_TEXT_LENGTH);
			inboxMessage.text[SMS_MAX_TEXT_LENGTH] = 0;
			migrationOk = EEPROM_Write((int32_t)targetAddress, (uint8_t *)&inboxMessage, (int)sizeof(inboxMessage));
		}

		for (uint8_t i = 0U; (i < migratedSentCount) && migrationOk; i++)
		{
			uint32_t legacyAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsLegacyStorageV4_t, sentMessages) + ((uint32_t)i * sizeof(smsLegacySentMessage_t)));
			uint32_t targetAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, sentMessages) + ((uint32_t)i * sizeof(smsSentMessage_t)));

			migrationOk = EEPROM_Read((int32_t)legacyAddress, (uint8_t *)&legacySentMessage, (int)sizeof(legacySentMessage));
			if (!migrationOk)
			{
				break;
			}

			memset(&sentMessage, 0, sizeof(sentMessage));
			sentMessage.destinationId = legacySentMessage.destinationId;
			strncpy(sentMessage.text, legacySentMessage.text, SMS_MAX_TEXT_LENGTH);
			sentMessage.text[SMS_MAX_TEXT_LENGTH] = 0;
			migrationOk = EEPROM_Write((int32_t)targetAddress, (uint8_t *)&sentMessage, (int)sizeof(sentMessage));
		}

		for (uint8_t i = 0U; (i < migratedQuickTextCount) && migrationOk; i++)
		{
			uint32_t legacyAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsLegacyStorageV4_t, quickTextMessages) + ((uint32_t)i * sizeof(smsLegacyQuickTextMessage_t)));
			uint32_t targetAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, quickTextMessages) + ((uint32_t)i * sizeof(smsQuickTextMessage_t)));

			migrationOk = EEPROM_Read((int32_t)legacyAddress, (uint8_t *)&legacyQuickTextMessage, (int)sizeof(legacyQuickTextMessage));
			if (!migrationOk)
			{
				break;
			}

			memset(&quickTextMessage, 0, sizeof(quickTextMessage));
			strncpy(quickTextMessage.title, legacyQuickTextMessage.title, SMS_QUICKTEXT_MAX_TITLE_LENGTH);
			quickTextMessage.title[SMS_QUICKTEXT_MAX_TITLE_LENGTH] = 0;
			strncpy(quickTextMessage.text, legacyQuickTextMessage.text, SMS_MAX_TEXT_LENGTH);
			quickTextMessage.text[SMS_MAX_TEXT_LENGTH] = 0;
			migrationOk = EEPROM_Write((int32_t)targetAddress, (uint8_t *)&quickTextMessage, (int)sizeof(quickTextMessage));
		}

		if (migrationOk)
		{
			migrationOk = smsStorageUpdateChecksum();
		}

		if (migrationOk)
		{
			inboxCount = migratedInboxCount;
			sentCount = migratedSentCount;
			quickTextCount = migratedQuickTextCount;
			inboxUnreadNotification = false;
			return;
		}

		inboxCount = 0U;
		sentCount = 0U;
		quickTextCount = 0U;
		inboxUnreadNotification = false;
		return;
	}

	{
		smsLegacyInboxStorageV1Header_t legacyV1Header;

		if (!EEPROM_Read(SMS_STORAGE_ADDRESS, (uint8_t *)&legacyV1Header, (int)sizeof(legacyV1Header)))
		{
			return;
		}

		if ((legacyV1Header.magic == SMS_STORAGE_MAGIC) &&
			(legacyV1Header.version == 1U) &&
			(legacyV1Header.messageCount <= SMS_INBOX_MAX_MESSAGES) &&
			smsStorageChecksumFromEeprom(SMS_STORAGE_ADDRESS,
				(uint32_t)sizeof(smsLegacyInboxStorageV1_t),
				(uint32_t)offsetof(smsLegacyInboxStorageV1_t, checksum),
				&checksum) &&
			(legacyV1Header.checksum == checksum))
		{
			smsStorageHeader_t migratedHeader;
			smsLegacyInboxMessage_t legacyInboxMessage;
			smsInboxMessage_t inboxMessage;
			bool migrationOk;
			uint8_t migratedInboxCount = (uint8_t)legacyV1Header.messageCount;

			memset(&migratedHeader, 0, sizeof(migratedHeader));
			migratedHeader.magic = SMS_STORAGE_MAGIC;
			migratedHeader.version = SMS_STORAGE_VERSION;
			migratedHeader.inboxCount = migratedInboxCount;

			migrationOk = smsStorageWriteHeader(&migratedHeader);

			for (uint8_t i = 0U; (i < migratedInboxCount) && migrationOk; i++)
			{
				uint32_t legacyAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsLegacyInboxStorageV1_t, messages) + ((uint32_t)i * sizeof(smsLegacyInboxMessage_t)));
				uint32_t targetAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, inboxMessages) + ((uint32_t)i * sizeof(smsInboxMessage_t)));

				migrationOk = EEPROM_Read((int32_t)legacyAddress, (uint8_t *)&legacyInboxMessage, (int)sizeof(legacyInboxMessage));
				if (!migrationOk)
				{
					break;
				}

				memset(&inboxMessage, 0, sizeof(inboxMessage));
				inboxMessage.sourceId = legacyInboxMessage.sourceId;
				strncpy(inboxMessage.text, legacyInboxMessage.text, SMS_MAX_TEXT_LENGTH);
				inboxMessage.text[SMS_MAX_TEXT_LENGTH] = 0;
				migrationOk = EEPROM_Write((int32_t)targetAddress, (uint8_t *)&inboxMessage, (int)sizeof(inboxMessage));
			}

			if (migrationOk)
			{
				migrationOk = smsStorageUpdateChecksum();
			}

			if (migrationOk)
			{
				inboxCount = migratedInboxCount;
				sentCount = 0U;
				quickTextCount = 0U;
				inboxUnreadNotification = false;
				return;
			}

			inboxCount = 0U;
			sentCount = 0U;
			quickTextCount = 0U;
			inboxUnreadNotification = false;
		}
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
		if ((smsIsPrintableCharacter(low)) || (low >= 0xA0U))
		{
			*mapped = low;
			*utf16Like = true;
			return true;
		}

		if (low == '\t')
		{
			*mapped = ' ';
			*utf16Like = true;
			return true;
		}
	}

	if (low == 0x00U)
	{
		if ((smsIsPrintableCharacter(high)) || (high >= 0xA0U))
		{
			*mapped = high;
			*utf16Like = true;
			return true;
		}

		if (high == '\t')
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
	smsStorageHeader_t header;
	smsInboxMessage_t shiftedMessage;
	smsInboxMessage_t newMessage;
	uint32_t baseAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, inboxMessages));
	uint8_t writeIndex;

	if ((text == NULL) || (text[0] == 0))
	{
		return;
	}

	if (!smsStorageReadHeader(&header))
	{
		memset(&header, 0, sizeof(header));
		header.magic = SMS_STORAGE_MAGIC;
		header.version = SMS_STORAGE_VERSION;
		header.inboxCount = inboxCount;
		header.sentCount = sentCount;
		header.quickTextCount = quickTextCount;
	}

	if (header.inboxCount < SMS_INBOX_MAX_MESSAGES)
	{
		writeIndex = (uint8_t)header.inboxCount;
		header.inboxCount++;
	}
	else
	{
		for (uint8_t i = 1U; i < SMS_INBOX_MAX_MESSAGES; i++)
		{
			uint32_t fromAddress = (uint32_t)(baseAddress + ((uint32_t)i * sizeof(smsInboxMessage_t)));
			uint32_t toAddress = (uint32_t)(baseAddress + ((uint32_t)(i - 1U) * sizeof(smsInboxMessage_t)));

			if (!EEPROM_Read((int32_t)fromAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)) ||
				!EEPROM_Write((int32_t)toAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)))
			{
				return;
			}
		}

		writeIndex = (uint8_t)(SMS_INBOX_MAX_MESSAGES - 1U);
	}

	memset(&newMessage, 0, sizeof(newMessage));
	newMessage.sourceId = sourceId;
	strncpy(newMessage.text, text, SMS_MAX_TEXT_LENGTH);
	newMessage.text[SMS_MAX_TEXT_LENGTH] = 0;

	if (EEPROM_Write((int32_t)(baseAddress + ((uint32_t)writeIndex * sizeof(smsInboxMessage_t))), (uint8_t *)&newMessage, (int)sizeof(newMessage)) &&
		smsStorageWriteHeader(&header) &&
		smsStorageUpdateChecksum())
	{
		inboxCount = (uint8_t)header.inboxCount;
		inboxUnreadNotification = true;
		smsStorageDirty = false;
		smsStorageDirtySinceTick = 0U;
	}
}

static void smsStoreSentMessageInternal(uint32_t destinationId, const char *text)
{
	smsStorageHeader_t header;
	smsSentMessage_t shiftedMessage;
	smsSentMessage_t newMessage;
	uint32_t baseAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, sentMessages));
	uint8_t writeIndex;

	if ((text == NULL) || (text[0] == 0) || (destinationId == 0U) || (destinationId > 0x00FFFFFFU))
	{
		return;
	}

	if (!smsStorageReadHeader(&header))
	{
		memset(&header, 0, sizeof(header));
		header.magic = SMS_STORAGE_MAGIC;
		header.version = SMS_STORAGE_VERSION;
		header.inboxCount = inboxCount;
		header.sentCount = sentCount;
		header.quickTextCount = quickTextCount;
	}

	if (header.sentCount < SMS_SENT_MAX_MESSAGES)
	{
		writeIndex = (uint8_t)header.sentCount;
		header.sentCount++;
	}
	else
	{
		for (uint8_t i = 1U; i < SMS_SENT_MAX_MESSAGES; i++)
		{
			uint32_t fromAddress = (uint32_t)(baseAddress + ((uint32_t)i * sizeof(smsSentMessage_t)));
			uint32_t toAddress = (uint32_t)(baseAddress + ((uint32_t)(i - 1U) * sizeof(smsSentMessage_t)));

			if (!EEPROM_Read((int32_t)fromAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)) ||
				!EEPROM_Write((int32_t)toAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)))
			{
				return;
			}
		}

		writeIndex = (uint8_t)(SMS_SENT_MAX_MESSAGES - 1U);
	}

	memset(&newMessage, 0, sizeof(newMessage));
	newMessage.destinationId = destinationId;
	strncpy(newMessage.text, text, SMS_MAX_TEXT_LENGTH);
	newMessage.text[SMS_MAX_TEXT_LENGTH] = 0;

	if (EEPROM_Write((int32_t)(baseAddress + ((uint32_t)writeIndex * sizeof(smsSentMessage_t))), (uint8_t *)&newMessage, (int)sizeof(newMessage)) &&
		smsStorageWriteHeader(&header) &&
		smsStorageUpdateChecksum())
	{
		sentCount = (uint8_t)header.sentCount;
		smsStorageDirty = false;
		smsStorageDirtySinceTick = 0U;
	}
}

static bool smsDecodeUtf16Payload(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	uint16_t probeIndex = 0U;
	uint16_t inIndex = 0U;
	uint16_t outIndex = 0U;
	uint16_t utf16PatternCount = 0U;
	uint16_t replacedCount = 0U;
	uint16_t littleEndianPatternCount = 0U;
	uint16_t bigEndianPatternCount = 0U;
	bool preferLittleEndian = true;

	if ((payload == NULL) || (textOut == NULL) || ((payloadLength & 0x01U) != 0U))
	{
		return false;
	}

	for (probeIndex = 0U; (probeIndex + 1U) < payloadLength; probeIndex += 2U)
	{
		uint8_t highProbe = payload[probeIndex];
		uint8_t lowProbe = payload[probeIndex + 1U];

		if ((highProbe == 0x00U) && smsIsPrintableCharacter(lowProbe))
		{
			bigEndianPatternCount++;
		}
		else if ((lowProbe == 0x00U) && smsIsPrintableCharacter(highProbe))
		{
			littleEndianPatternCount++;
		}
	}

	preferLittleEndian = (littleEndianPatternCount >= bigEndianPatternCount);

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
		else if (smsIsPrintableCharacter(high) || smsIsPrintableCharacter(low))
		{
			if (smsIsPrintableCharacter(high) && smsIsPrintableCharacter(low))
			{
				c = (preferLittleEndian ? high : low);
			}
			else if (smsIsPrintableCharacter(high))
			{
				c = high;
			}
			else
			{
				c = low;
			}

			utf16PatternCount++;
		}
		else
		{
			c = '?';
			replacedCount++;
		}

		if (outIndex >= SMS_MAX_TEXT_LENGTH)
		{
			break;
		}

		if (c == '\t')
		{
			c = ' ';
		}

		if (c == '\r')
		{
			c = '\n';
		}

		if ((c == '\n') && (outIndex > 0U) && (textOut[outIndex - 1U] == '\n'))
		{
			continue;
		}

		textOut[outIndex++] = (char)c;
	}

	textOut[outIndex] = 0;

	if ((outIndex == 0U) || (utf16PatternCount < 2U))
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

	for (uint16_t i = 0U; (i < payloadLength) && (outIndex < SMS_MAX_TEXT_LENGTH); i++)
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

static uint16_t smsCountCharOccurrence(const char *text, char value)
{
	uint16_t count = 0U;

	if (text == NULL)
	{
		return 0U;
	}

	for (uint16_t i = 0U; text[i] != 0; i++)
	{
		if (text[i] == value)
		{
			count++;
		}
	}

	return count;
}

static int32_t smsScoreDecodedText(const char *text)
{
	uint16_t length;
	uint16_t questionCount;
	uint16_t alphaNumCount = 0U;
	uint16_t spaceCount = 0U;
	uint16_t newlineCount = 0U;
	uint16_t controlCount = 0U;
	uint16_t punctuationCount = 0U;
	uint16_t wordTransitions = 0U;
	bool inWord = false;
	int32_t score;

	if ((text == NULL) || (text[0] == 0))
	{
		return -32768;
	}

	length = (uint16_t)strlen(text);
	if (length < 3U)
	{
		return -32768;
	}

	questionCount = smsCountCharOccurrence(text, '?');

	for (uint16_t i = 0U; text[i] != 0; i++)
	{
		char c = text[i];
		bool isAlphaNum = (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')));

		if (isAlphaNum)
		{
			alphaNumCount++;
			if (!inWord)
			{
				wordTransitions++;
				inWord = true;
			}
		}
		else
		{
			if (c == ' ')
			{
				spaceCount++;
			}
			else if (c == '\n')
			{
				newlineCount++;
			}
			else if ((c >= 0x20) && (c <= 0x7E))
			{
				punctuationCount++;
			}
			else
			{
				controlCount++;
			}

			inWord = false;
		}
	}

	score = (int32_t)length * 10;
	score += (int32_t)alphaNumCount * 3;
	score += (int32_t)wordTransitions * 8;
	score += (int32_t)punctuationCount;
	score += (int32_t)newlineCount * 2;
	score -= (int32_t)questionCount * 24;
	score -= (int32_t)controlCount * 40;

	if ((spaceCount == 0U) && (newlineCount == 0U) && (length > 20U))
	{
		score -= 30;
	}

	if ((questionCount * 2U) > length)
	{
		score -= 50;
	}

	return score;
}

static bool smsIsBetterDecodedCandidate(const char *currentText, const char *candidateText)
{
	int32_t currentScore;
	int32_t candidateScore;
	uint16_t currentLength;
	uint16_t candidateLength;

	if ((candidateText == NULL) || (candidateText[0] == 0))
	{
		return false;
	}

	if ((currentText == NULL) || (currentText[0] == 0))
	{
		return true;
	}

	currentScore = smsScoreDecodedText(currentText);
	candidateScore = smsScoreDecodedText(candidateText);
	currentLength = (uint16_t)strlen(currentText);
	candidateLength = (uint16_t)strlen(candidateText);

	if (candidateScore > (currentScore + 4))
	{
		return true;
	}

	if ((candidateScore >= currentScore) && (candidateLength > (uint16_t)(currentLength + 4U)))
	{
		return true;
	}

	return false;
}

static bool smsMergeDecodedCandidate(char *decodedText, const char *candidateText)
{
	char backup[SMS_MAX_TEXT_LENGTH + 1U];
	int32_t oldScore;
	int32_t newScore;
	bool changed = false;

	if ((decodedText == NULL) || (candidateText == NULL) || (decodedText[0] == 0) || (candidateText[0] == 0))
	{
		return false;
	}

	strncpy(backup, decodedText, SMS_MAX_TEXT_LENGTH);
	backup[SMS_MAX_TEXT_LENGTH] = 0;
	oldScore = smsScoreDecodedText(decodedText);

	for (uint16_t i = 0U; i < SMS_MAX_TEXT_LENGTH; i++)
	{
		char currentChar = decodedText[i];
		char candidateChar = candidateText[i];

		if (currentChar == 0)
		{
			if (candidateChar == 0)
			{
				break;
			}

			if (candidateChar != '?')
			{
				decodedText[i] = candidateChar;
				if (i < SMS_MAX_TEXT_LENGTH)
				{
					decodedText[i + 1U] = 0;
				}
				changed = true;
			}

			continue;
		}

		if ((currentChar == '?') && (candidateChar != 0) && (candidateChar != '?'))
		{
			decodedText[i] = candidateChar;
			changed = true;
		}
		else if ((currentChar == ' ') && (candidateChar == '\n'))
		{
			decodedText[i] = '\n';
			changed = true;
		}
	}

	if (!changed)
	{
		return false;
	}

	newScore = smsScoreDecodedText(decodedText);
	if (newScore < oldScore)
	{
		strncpy(decodedText, backup, SMS_MAX_TEXT_LENGTH);
		decodedText[SMS_MAX_TEXT_LENGTH] = 0;
		return false;
	}

	return true;
}

static bool smsTryAdoptDecodedCandidate(char *decodedText, const char *candidateText)
{
	if ((decodedText == NULL) || (candidateText == NULL) || (candidateText[0] == 0))
	{
		return false;
	}

	if ((decodedText[0] == 0) || smsIsBetterDecodedCandidate(decodedText, candidateText))
	{
		strncpy(decodedText, candidateText, SMS_MAX_TEXT_LENGTH);
		decodedText[SMS_MAX_TEXT_LENGTH] = 0;
		return true;
	}

	return smsMergeDecodedCandidate(decodedText, candidateText);
}

static bool smsTryDecodeWindowDirect(const uint8_t *payload, uint16_t payloadLength, bool allowAscii, char *decodedText, char *scratchText)
{
	uint16_t boundedLength;
	uint16_t utf16Length;
	bool improved = false;

	if ((payload == NULL) || (decodedText == NULL) || (scratchText == NULL) || (payloadLength == 0U))
	{
		return false;
	}

	boundedLength = payloadLength;
	if (boundedLength > SMS_MAX_UTF16_PAYLOAD_BYTES)
	{
		boundedLength = SMS_MAX_UTF16_PAYLOAD_BYTES;
	}

	utf16Length = (uint16_t)(boundedLength & 0xFFFEU);
	if ((utf16Length >= 2U) && smsDecodeUtf16Payload(payload, utf16Length, scratchText))
	{
		improved |= smsTryAdoptDecodedCandidate(decodedText, scratchText);
	}

	if ((boundedLength >= 3U) && smsDecodeUtf8Payload(payload, boundedLength, scratchText))
	{
		improved |= smsTryAdoptDecodedCandidate(decodedText, scratchText);
	}

	if (allowAscii && (boundedLength >= 1U) && smsDecodeAsciiPayload(payload, boundedLength, scratchText))
	{
		improved |= smsTryAdoptDecodedCandidate(decodedText, scratchText);
	}

	return improved;
}

static void smsTryDecodeWindowNeighborhood(const uint8_t *payload, uint16_t payloadLength, uint16_t centerOffset, uint16_t radius, uint16_t step, bool allowAscii, char *decodedText, char *scratchText)
{
	uint16_t startOffset;
	uint16_t endOffset;

	if ((payload == NULL) || (decodedText == NULL) || (scratchText == NULL) || (payloadLength == 0U) || (centerOffset >= payloadLength))
	{
		return;
	}

	if (step == 0U)
	{
		step = 1U;
	}

	startOffset = ((centerOffset > radius) ? (uint16_t)(centerOffset - radius) : 0U);
	endOffset = (uint16_t)(centerOffset + radius);
	if (endOffset >= payloadLength)
	{
		endOffset = (uint16_t)(payloadLength - 1U);
	}

	for (uint16_t offset = startOffset; offset <= endOffset; offset = (uint16_t)(offset + step))
	{
		uint16_t candidateLength = (uint16_t)(payloadLength - offset);
		smsTryDecodeWindowDirect(&payload[offset], candidateLength, allowAscii, decodedText, scratchText);

		if ((endOffset - offset) < step)
		{
			break;
		}
	}
}

static char smsMapUnicodeToAscii(uint32_t codePoint)
{
	if ((codePoint >= 0x20U) && (codePoint <= 0x7EU))
	{
		return (char)codePoint;
	}

	switch (codePoint)
	{
		case 0x0009U:
			return ' ';
		case 0x000AU:
		case 0x000DU:
			return '\n';
		case 0x00A0U:
			return ' ';
		case 0x2018U:
		case 0x2019U:
			return '\'';
		case 0x201CU:
		case 0x201DU:
			return '"';
		case 0x2013U:
		case 0x2014U:
			return '-';
		case 0x00C0U:
		case 0x00C1U:
		case 0x00C2U:
		case 0x00C3U:
		case 0x00C4U:
		case 0x00C5U:
		case 0x00E0U:
		case 0x00E1U:
		case 0x00E2U:
		case 0x00E3U:
		case 0x00E4U:
		case 0x00E5U:
			return 'a';
		case 0x00C7U:
		case 0x00E7U:
			return 'c';
		case 0x00C8U:
		case 0x00C9U:
		case 0x00CAU:
		case 0x00CBU:
		case 0x00E8U:
		case 0x00E9U:
		case 0x00EAU:
		case 0x00EBU:
			return 'e';
		case 0x00CCU:
		case 0x00CDU:
		case 0x00CEU:
		case 0x00CFU:
		case 0x00ECU:
		case 0x00EDU:
		case 0x00EEU:
		case 0x00EFU:
			return 'i';
		case 0x00D1U:
		case 0x00F1U:
			return 'n';
		case 0x00D2U:
		case 0x00D3U:
		case 0x00D4U:
		case 0x00D5U:
		case 0x00D6U:
		case 0x00D8U:
		case 0x00F2U:
		case 0x00F3U:
		case 0x00F4U:
		case 0x00F5U:
		case 0x00F6U:
		case 0x00F8U:
			return 'o';
		case 0x00D9U:
		case 0x00DAU:
		case 0x00DBU:
		case 0x00DCU:
		case 0x00F9U:
		case 0x00FAU:
		case 0x00FBU:
		case 0x00FCU:
			return 'u';
		case 0x00DDU:
		case 0x00FDU:
		case 0x00FFU:
			return 'y';
		case 0x00DFU:
			return 's';
		default:
			return '?';
	}
}

static bool smsDecodeUtf8Payload(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	uint16_t outIndex = 0U;
	uint16_t printableCount = 0U;
	uint16_t replacedCount = 0U;
	uint16_t inIndex = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength == 0U))
	{
		return false;
	}

	while ((inIndex < payloadLength) && (outIndex < SMS_MAX_TEXT_LENGTH))
	{
		uint8_t b0 = payload[inIndex];
		uint32_t codePoint = 0xFFFDU;
		uint8_t seqLen = 1U;
		bool valid = true;
		char c;

		if (b0 == 0x00U)
		{
			break;
		}

		if (b0 < 0x80U)
		{
			codePoint = b0;
		}
		else if ((b0 & 0xE0U) == 0xC0U)
		{
			seqLen = 2U;
			if ((inIndex + seqLen) > payloadLength)
			{
				valid = false;
			}
			else
			{
				uint8_t b1 = payload[inIndex + 1U];
				if ((b1 & 0xC0U) != 0x80U)
				{
					valid = false;
				}
				else
				{
					codePoint = (uint32_t)(((uint32_t)(b0 & 0x1FU) << 6) | (uint32_t)(b1 & 0x3FU));
					if (codePoint < 0x80U)
					{
						valid = false;
					}
				}
			}
		}
		else if ((b0 & 0xF0U) == 0xE0U)
		{
			seqLen = 3U;
			if ((inIndex + seqLen) > payloadLength)
			{
				valid = false;
			}
			else
			{
				uint8_t b1 = payload[inIndex + 1U];
				uint8_t b2 = payload[inIndex + 2U];
				if (((b1 & 0xC0U) != 0x80U) || ((b2 & 0xC0U) != 0x80U))
				{
					valid = false;
				}
				else
				{
					codePoint = (uint32_t)(((uint32_t)(b0 & 0x0FU) << 12) | ((uint32_t)(b1 & 0x3FU) << 6) | (uint32_t)(b2 & 0x3FU));
					if ((codePoint < 0x800U) || ((codePoint >= 0xD800U) && (codePoint <= 0xDFFFU)))
					{
						valid = false;
					}
				}
			}
		}
		else if ((b0 & 0xF8U) == 0xF0U)
		{
			seqLen = 4U;
			if ((inIndex + seqLen) > payloadLength)
			{
				valid = false;
			}
			else
			{
				uint8_t b1 = payload[inIndex + 1U];
				uint8_t b2 = payload[inIndex + 2U];
				uint8_t b3 = payload[inIndex + 3U];
				if (((b1 & 0xC0U) != 0x80U) || ((b2 & 0xC0U) != 0x80U) || ((b3 & 0xC0U) != 0x80U))
				{
					valid = false;
				}
				else
				{
					codePoint = (uint32_t)(((uint32_t)(b0 & 0x07U) << 18) | ((uint32_t)(b1 & 0x3FU) << 12) | ((uint32_t)(b2 & 0x3FU) << 6) | (uint32_t)(b3 & 0x3FU));
					if ((codePoint < 0x10000U) || (codePoint > 0x10FFFFU))
					{
						valid = false;
					}
				}
			}
		}
		else
		{
			valid = false;
		}

		if (!valid)
		{
			codePoint = 0xFFFDU;
			seqLen = 1U;
		}

		inIndex = (uint16_t)(inIndex + seqLen);
		c = smsMapUnicodeToAscii(codePoint);
		textOut[outIndex++] = c;

		if (c == '?')
		{
			replacedCount++;
		}
		else
		{
			printableCount++;
		}
	}

	textOut[outIndex] = 0;

	if ((outIndex < 3U) || (printableCount == 0U))
	{
		return false;
	}

	return ((replacedCount * 3U) <= outIndex);
}

static bool smsDecodeUtf16LeRun(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut)
{
	uint16_t bestOffset = 0U;
	uint16_t bestLength = 0U;
	uint16_t bestAlphaNumCount = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength < 4U))
	{
		return false;
	}

	for (uint16_t offset = 0U; (offset + 1U) < payloadLength; offset++)
	{
		uint16_t cursor = offset;
		uint16_t runLength = 0U;
		uint16_t alphaNumCount = 0U;

		while ((cursor + 1U) < payloadLength)
		{
			uint8_t low = payload[cursor];
			uint8_t high = payload[cursor + 1U];

			if ((low == 0x00U) && (high == 0x00U))
			{
				break;
			}

			if ((high != 0x00U) || !smsIsPrintableCharacter(low))
			{
				break;
			}

			runLength++;
			if (((low >= 'A') && (low <= 'Z')) || ((low >= 'a') && (low <= 'z')) || ((low >= '0') && (low <= '9')))
			{
				alphaNumCount++;
			}

			cursor = (uint16_t)(cursor + 2U);
			if (runLength >= SMS_MAX_TEXT_LENGTH)
			{
				break;
			}
		}

		if ((runLength > bestLength) || ((runLength == bestLength) && (alphaNumCount > bestAlphaNumCount)))
		{
			bestOffset = offset;
			bestLength = runLength;
			bestAlphaNumCount = alphaNumCount;
		}
	}

	if ((bestLength < SMS_RX_MIN_UTF16_LE_RUN_CHARS) || (bestAlphaNumCount < 3U))
	{
		return false;
	}

	for (uint16_t i = 0U; i < bestLength; i++)
	{
		uint8_t c = payload[bestOffset + (uint16_t)(i * 2U)];
		textOut[i] = (char)c;
	}
	textOut[bestLength] = 0;

	if (offsetOut != NULL)
	{
		*offsetOut = bestOffset;
	}

	return true;
}

static bool smsDecodeUtf16BeRun(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut)
{
	uint16_t bestOffset = 0U;
	uint16_t bestLength = 0U;
	uint16_t bestAlphaNumCount = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength < 4U))
	{
		return false;
	}

	for (uint16_t offset = 0U; (offset + 1U) < payloadLength; offset++)
	{
		uint16_t cursor = offset;
		uint16_t runLength = 0U;
		uint16_t alphaNumCount = 0U;

		while ((cursor + 1U) < payloadLength)
		{
			uint8_t high = payload[cursor];
			uint8_t low = payload[cursor + 1U];

			if ((high == 0x00U) && (low == 0x00U))
			{
				break;
			}

			if ((high != 0x00U) || !smsIsPrintableCharacter(low))
			{
				break;
			}

			runLength++;
			if (((low >= 'A') && (low <= 'Z')) || ((low >= 'a') && (low <= 'z')) || ((low >= '0') && (low <= '9')))
			{
				alphaNumCount++;
			}

			cursor = (uint16_t)(cursor + 2U);
			if (runLength >= SMS_MAX_TEXT_LENGTH)
			{
				break;
			}
		}

		if ((runLength > bestLength) || ((runLength == bestLength) && (alphaNumCount > bestAlphaNumCount)))
		{
			bestOffset = offset;
			bestLength = runLength;
			bestAlphaNumCount = alphaNumCount;
		}
	}

	if ((bestLength < SMS_RX_MIN_UTF16_LE_RUN_CHARS) || (bestAlphaNumCount < 3U))
	{
		return false;
	}

	for (uint16_t i = 0U; i < bestLength; i++)
	{
		uint8_t c = payload[bestOffset + (uint16_t)(i * 2U) + 1U];
		textOut[i] = (char)c;
	}
	textOut[bestLength] = 0;

	if (offsetOut != NULL)
	{
		*offsetOut = bestOffset;
	}

	return true;
}

static bool smsDecodeUtf16PayloadByScan(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut)
{
	char candidate[SMS_MAX_TEXT_LENGTH + 1U];
	int32_t bestScore = -32768;
	uint16_t bestLength = 0U;
	uint16_t bestOffset = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength < 4U))
	{
		return false;
	}

	for (uint16_t offset = 0U; (offset + 4U) <= payloadLength; offset++)
	{
		uint16_t candidateLength = (uint16_t)(payloadLength - offset);
		uint16_t decodedLength;
		uint16_t questionCount;
		int32_t score;

		if (candidateLength > SMS_MAX_UTF16_PAYLOAD_BYTES)
		{
			candidateLength = SMS_MAX_UTF16_PAYLOAD_BYTES;
		}

		candidateLength = (uint16_t)(candidateLength & 0xFFFEU);
		if (candidateLength < 4U)
		{
			continue;
		}

		if (!smsDecodeUtf16Payload(&payload[offset], candidateLength, candidate))
		{
			continue;
		}

		decodedLength = (uint16_t)strlen(candidate);
		if (decodedLength < 4U)
		{
			continue;
		}

		questionCount = smsCountCharOccurrence(candidate, '?');
		score = (int32_t)decodedLength * 8 - (int32_t)questionCount * 20;

		if ((score > bestScore) || ((score == bestScore) && (decodedLength > bestLength)))
		{
			bestScore = score;
			bestLength = decodedLength;
			bestOffset = offset;
			strncpy(textOut, candidate, SMS_MAX_TEXT_LENGTH);
			textOut[SMS_MAX_TEXT_LENGTH] = 0;

			if (bestLength >= (SMS_MAX_TEXT_LENGTH - 1U))
			{
				break;
			}
		}
	}

	if (bestLength == 0U)
	{
		return false;
	}

	if (offsetOut != NULL)
	{
		*offsetOut = bestOffset;
	}

	return true;
}

static bool smsDecodeUtf8PayloadByScan(const uint8_t *payload, uint16_t payloadLength, char *textOut, uint16_t *offsetOut)
{
	char candidate[SMS_MAX_TEXT_LENGTH + 1U];
	int32_t bestScore = -32768;
	uint16_t bestLength = 0U;
	uint16_t bestOffset = 0U;

	if ((payload == NULL) || (textOut == NULL) || (payloadLength < 4U))
	{
		return false;
	}

	for (uint16_t offset = 0U; (offset + 4U) <= payloadLength; offset++)
	{
		uint16_t candidateLength = (uint16_t)(payloadLength - offset);
		uint16_t decodedLength;
		uint16_t questionCount;
		int32_t score;

		if (candidateLength > SMS_MAX_UTF16_PAYLOAD_BYTES)
		{
			candidateLength = SMS_MAX_UTF16_PAYLOAD_BYTES;
		}

		if (!smsDecodeUtf8Payload(&payload[offset], candidateLength, candidate))
		{
			continue;
		}

		decodedLength = (uint16_t)strlen(candidate);
		if (decodedLength < 4U)
		{
			continue;
		}

		questionCount = smsCountCharOccurrence(candidate, '?');
		score = (int32_t)decodedLength * 8 - (int32_t)questionCount * 20;

		if ((score > bestScore) || ((score == bestScore) && (decodedLength > bestLength)))
		{
			bestScore = score;
			bestLength = decodedLength;
			bestOffset = offset;
			strncpy(textOut, candidate, SMS_MAX_TEXT_LENGTH);
			textOut[SMS_MAX_TEXT_LENGTH] = 0;

			if (bestLength >= (SMS_MAX_TEXT_LENGTH - 1U))
			{
				break;
			}
		}
	}

	if (bestLength == 0U)
	{
		return false;
	}

	if (offsetOut != NULL)
	{
		*offsetOut = bestOffset;
	}

	return true;
}

static void smsResetRxAssembly(void)
{
	memset(&rxAssembly, 0, sizeof(rxAssembly));
	memset(smsRxPayloadBuffer, 0, sizeof(smsRxPayloadBuffer));
}

static void smsScheduleAckResponse(uint32_t destinationId, uint8_t ackProfile)
{
	if ((destinationId == 0U) || (destinationId > 0x00FFFFFFU))
	{
		return;
	}

	ackResponseTracking.destinationId = destinationId;
	ackResponseTracking.ackProfile = ((ackProfile == SMS_ACK_PROFILE_MODERN) ? SMS_ACK_PROFILE_MODERN : SMS_ACK_PROFILE_LEGACY);
	ackResponseTracking.active = true;
}

static bool smsQueueAckResponseMessage(uint32_t destinationId, uint32_t sourceId, uint8_t ackProfile)
{
	uint16_t crc;

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

	if (ackProfile == SMS_ACK_PROFILE_MODERN)
	{
		queuedMessage.dataHeader[1] = 0x40U; // Repeater/hotspot response profile.
		queuedMessage.dataHeader[9] = 0x08U;
	}
	else
	{
		queuedMessage.dataHeader[1] = 0x00U; // Legacy simplex/original firmware response profile.
		queuedMessage.dataHeader[9] = 0x00U;
	}

	crc = smsCrc16Ccitt(queuedMessage.dataHeader, 10U);
	queuedMessage.dataHeader[10] = (uint8_t)((crc >> 8) & 0xFFU) ^ 0xCCU;
	queuedMessage.dataHeader[11] = (uint8_t)(crc & 0xFFU) ^ 0xCCU;

	queuedMessageValid = true;
	return true;
}

static bool smsHandleIncomingResponsePdu(const uint8_t *frame)
{
	uint8_t dataPacketFormat;
	uint8_t sapType;
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

	// Treat as ACK/response only when the response payload marker is present.
	// This avoids swallowing valid inbound SMS headers that use dataPacketFormat 0x01.
	sapType = (uint8_t)(frame[1] & 0xF0U);
	if (!(((sapType == 0x40U) && (frame[8] == 0x00U) && ((frame[9] == 0x08U) || (frame[9] == 0x00U))) ||
		((sapType == 0x00U) && (frame[8] == 0x00U) && (frame[9] == 0x00U))))
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

	for (uint16_t i = 12U; i < 20U; i += 2U)
	{
		sum += ((uint32_t)ipPacket[i] << 8) + (uint32_t)ipPacket[i + 1U];
	}
	sum += 0x0011U;
	sum += (uint32_t)udpLength;

	for (uint16_t i = 0U; i < udpLength; i += 2U)
	{
		sum += ((uint32_t)ipPacket[20U + i] << 8);
		if ((i + 1U) < udpLength)
		{
			sum += (uint32_t)ipPacket[20U + i + 1U];
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
	uint8_t utf16Payload[SMS_MAX_UTF16_PAYLOAD_BYTES];
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

	if (*payloadLength > SMS_MAX_TRANSPORT_BYTES)
	{
		return SMS_PACK_ERROR_TOO_LONG;
	}

	memset(payload, 0, SMS_MAX_TRANSPORT_BYTES);
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
	uint8_t headerCopy[20U];
	uint8_t packetCopy[SMS_MAX_TRANSPORT_BYTES];

	if ((payload == NULL) || (textOut == NULL) || (totalLength < (SMS_MOTOROLA_TEXT_OFFSET + SMS_STANDARD_CRC32_BYTES)))
	{
		return false;
	}

	ipPacketLength = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
	if ((ipPacketLength < SMS_MOTOROLA_TEXT_OFFSET) ||
		(ipPacketLength > (totalLength - SMS_STANDARD_CRC32_BYTES)) ||
		((uint16_t)(ipPacketLength + padOctets + SMS_STANDARD_CRC32_BYTES) != totalLength))
	{
		return false;
	}

	if (((payload[0] & 0xF0U) != 0x40U) || ((payload[0] & 0x0FU) != 0x05U) || (payload[9] != SMS_MOTOROLA_IPV4_PROTOCOL))
	{
		return false;
	}

	memcpy(headerCopy, payload, sizeof(headerCopy));
	headerChecksum = (uint16_t)(((uint16_t)payload[10] << 8) | payload[11]);
	headerCopy[10] = 0x00U;
	headerCopy[11] = 0x00U;
	if (smsIpHeaderChecksum(headerCopy, sizeof(headerCopy)) != headerChecksum)
	{
		return false;
	}

	udpLength = (uint16_t)(((uint16_t)payload[24] << 8) | payload[25]);
	if ((udpLength < 18U) || ((uint16_t)(20U + udpLength) != ipPacketLength))
	{
		return false;
	}

	srcPort = (uint16_t)(((uint16_t)payload[20] << 8) | payload[21]);
	dstPort = (uint16_t)(((uint16_t)payload[22] << 8) | payload[23]);
	if ((srcPort != SMS_MOTOROLA_UDP_PORT) && (dstPort != SMS_MOTOROLA_UDP_PORT))
	{
		return false;
	}

	memcpy(packetCopy, payload, ipPacketLength);
	udpChecksum = (uint16_t)(((uint16_t)payload[26] << 8) | payload[27]);
	packetCopy[26] = 0x00U;
	packetCopy[27] = 0x00U;
	if ((udpChecksum != 0U) && (smsUdpChecksum(packetCopy, udpLength) != udpChecksum))
	{
		return false;
	}

	if (payload[28] != 0x00U)
	{
		return false;
	}

	internalLength = (uint16_t)payload[29];
	textByteLength = (uint16_t)(udpLength - 18U);
	if ((internalLength != (textByteLength + 8U)) ||
		((payload[30] != 0xA0U) && (payload[30] != 0xE0U)) ||
		(payload[31] != 0x00U))
	{
		return false;
	}

	if ((payload[33] != 0x04U) || (payload[34] != 0x0DU) || (payload[35] != 0x00U) ||
		(payload[36] != 0x0AU) || (payload[37] != 0x00U))
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
		(textByteLength > SMS_MAX_UTF16_PAYLOAD_BYTES) ||
		((uint16_t)(SMS_MOTOROLA_TEXT_OFFSET + textByteLength) != ipPacketLength))
	{
		return false;
	}

	return smsDecodeUtf16Payload(&payload[SMS_MOTOROLA_TEXT_OFFSET], textByteLength, textOut);
}

static bool smsDecodeStandardPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, char *textOut)
{
	uint16_t ipPacketLength;
	uint16_t udpLength;
	uint16_t srcPort;
	uint16_t dstPort;
	uint16_t headerChecksum;
	uint16_t udpChecksum;
	uint16_t textByteLength;
	uint32_t expectedCrc32;
	uint32_t receivedCrc32;
	uint8_t headerCopy[20U];
	uint8_t packetCopy[SMS_MAX_TRANSPORT_BYTES];

	if ((payload == NULL) || (textOut == NULL) || (totalLength < (SMS_STANDARD_TEXT_OFFSET + SMS_STANDARD_CRC32_BYTES)))
	{
		return false;
	}

	ipPacketLength = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
	if ((ipPacketLength < SMS_STANDARD_TEXT_OFFSET) ||
		(ipPacketLength > (totalLength - SMS_STANDARD_CRC32_BYTES)) ||
		((uint16_t)(ipPacketLength + padOctets + SMS_STANDARD_CRC32_BYTES) != totalLength))
	{
		return false;
	}

	if (((payload[0] & 0xF0U) != 0x40U) || ((payload[0] & 0x0FU) != 0x05U) || (payload[9] != SMS_MOTOROLA_IPV4_PROTOCOL))
	{
		return false;
	}

	memcpy(headerCopy, payload, sizeof(headerCopy));
	headerChecksum = (uint16_t)(((uint16_t)payload[10] << 8) | payload[11]);
	headerCopy[10] = 0x00U;
	headerCopy[11] = 0x00U;
	if (smsIpHeaderChecksum(headerCopy, sizeof(headerCopy)) != headerChecksum)
	{
		return false;
	}

	udpLength = (uint16_t)(((uint16_t)payload[24] << 8) | payload[25]);
	if ((udpLength < 12U) || ((uint16_t)(20U + udpLength) != ipPacketLength))
	{
		return false;
	}

	srcPort = (uint16_t)(((uint16_t)payload[20] << 8) | payload[21]);
	dstPort = (uint16_t)(((uint16_t)payload[22] << 8) | payload[23]);
	if ((srcPort != SMS_STANDARD_UDP_PORT) && (dstPort != SMS_STANDARD_UDP_PORT))
	{
		return false;
	}

	memcpy(packetCopy, payload, ipPacketLength);
	udpChecksum = (uint16_t)(((uint16_t)payload[26] << 8) | payload[27]);
	packetCopy[26] = 0x00U;
	packetCopy[27] = 0x00U;
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
		(textByteLength > SMS_MAX_UTF16_PAYLOAD_BYTES) ||
		((uint16_t)(SMS_STANDARD_TEXT_OFFSET + textByteLength) != ipPacketLength))
	{
		return false;
	}

	return smsDecodeUtf16Payload(&payload[SMS_STANDARD_TEXT_OFFSET], textByteLength, textOut);
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

		if (index >= SMS_MAX_UTF16_PAYLOAD_BYTES)
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
	uint8_t payload[SMS_MAX_DATA_BLOCKS * SMS_BLOCK_DATA_BYTES];
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
	strncpy(outgoingTracking.text, text, SMS_MAX_TEXT_LENGTH);
	outgoingTracking.text[SMS_MAX_TEXT_LENGTH] = 0;
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

static bool smsDecodeCurrentRxBuffers(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets, uint32_t sourceId)
{
	uint8_t effectivePadOctets = padOctets;
	uint16_t payloadLength;
	uint16_t textDecodeLength;
	uint16_t scanStartOffset = 0U;
	uint16_t scanLength;
	const uint8_t *scanPayload;
	uint16_t ipPacketLength = 0U;
	uint16_t udpPayloadOffset = 28U;
	uint16_t udpPayloadLength = 0U;
	uint16_t standardTextOffsetLength = 0U;
	uint16_t motorolaTextOffsetLength = 0U;
	uint16_t utf16ScanOffset = 0U;
	uint16_t utf8ScanOffset = 0U;
	uint16_t utf16RunOffset = 0U;
	uint16_t utf16BeRunOffset = 0U;
	bool hasMotorolaSignature = false;
	bool hasMotorolaPort = false;
	bool hasStandardPort = false;
	bool decoded;
	char *decodedText = smsDecodeTextBuffers[SMS_DECODE_BUF_MAIN];
	char *scratchText = smsDecodeTextBuffers[SMS_DECODE_BUF_SCRATCH];

	if (payload == NULL)
	{
		return false;
	}

	for (uint8_t i = 0U; i < SMS_DECODE_BUF_COUNT; i++)
	{
		smsDecodeTextBuffers[i][0] = 0;
	}

	if ((totalLength == 0U) || (effectivePadOctets >= totalLength))
	{
		return false;
	}

	if ((effectivePadOctets >= SMS_BLOCK_DATA_BYTES) && (totalLength > SMS_STANDARD_CRC32_BYTES) && (totalLength >= 4U))
	{
		uint16_t ipPacketLengthInferred = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
		if ((ipPacketLengthInferred >= 20U) && (ipPacketLengthInferred <= (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES)))
		{
			uint16_t inferredPadOctets = (uint16_t)(totalLength - SMS_STANDARD_CRC32_BYTES - ipPacketLengthInferred);
			if (inferredPadOctets < SMS_BLOCK_DATA_BYTES)
			{
				effectivePadOctets = (uint8_t)inferredPadOctets;
			}
		}
	}

	if (effectivePadOctets >= SMS_BLOCK_DATA_BYTES)
	{
		effectivePadOctets = 0U;
	}

	payloadLength = (uint16_t)(totalLength - effectivePadOctets);
	textDecodeLength = payloadLength;

	if (payloadLength >= 4U)
	{
		ipPacketLength = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
		if ((ipPacketLength >= 20U) && (ipPacketLength <= payloadLength))
		{
			textDecodeLength = ipPacketLength;
		}
	}

	if (textDecodeLength > payloadLength)
	{
		textDecodeLength = payloadLength;
	}

	if (textDecodeLength >= 24U)
	{
		uint16_t srcPort = (uint16_t)(((uint16_t)payload[20] << 8) | payload[21]);
		uint16_t dstPort = (uint16_t)(((uint16_t)payload[22] << 8) | payload[23]);
		hasMotorolaPort = ((srcPort == SMS_MOTOROLA_UDP_PORT) || (dstPort == SMS_MOTOROLA_UDP_PORT));
		hasStandardPort = ((srcPort == SMS_STANDARD_UDP_PORT) || (dstPort == SMS_STANDARD_UDP_PORT));
	}

	if ((ipPacketLength >= udpPayloadOffset) && (ipPacketLength <= textDecodeLength))
	{
		udpPayloadLength = (uint16_t)(ipPacketLength - udpPayloadOffset);
	}
	else if (textDecodeLength > udpPayloadOffset)
	{
		udpPayloadLength = (uint16_t)(textDecodeLength - udpPayloadOffset);
	}

	if (textDecodeLength > SMS_STANDARD_TEXT_OFFSET)
	{
		standardTextOffsetLength = (uint16_t)(textDecodeLength - SMS_STANDARD_TEXT_OFFSET);
		if (standardTextOffsetLength > SMS_MAX_UTF16_PAYLOAD_BYTES)
		{
			standardTextOffsetLength = SMS_MAX_UTF16_PAYLOAD_BYTES;
		}
	}

	if (textDecodeLength > SMS_MOTOROLA_TEXT_OFFSET)
	{
		motorolaTextOffsetLength = (uint16_t)(textDecodeLength - SMS_MOTOROLA_TEXT_OFFSET);
		if (motorolaTextOffsetLength > SMS_MAX_UTF16_PAYLOAD_BYTES)
		{
			motorolaTextOffsetLength = SMS_MAX_UTF16_PAYLOAD_BYTES;
		}

		hasMotorolaSignature =
			(payload[28] == 0x00U) &&
			((payload[30] == 0xA0U) || (payload[30] == 0xE0U)) &&
			(payload[31] == 0x00U) &&
			(payload[33] == 0x04U) &&
			(payload[34] == 0x0DU) &&
			(payload[35] == 0x00U) &&
			(payload[36] == 0x0AU) &&
			(payload[37] == 0x00U);
	}

	if ((hasMotorolaSignature || hasMotorolaPort) && (textDecodeLength > SMS_MOTOROLA_TEXT_OFFSET))
	{
		scanStartOffset = SMS_MOTOROLA_TEXT_OFFSET;
	}
	else if (hasStandardPort && (textDecodeLength > SMS_STANDARD_TEXT_OFFSET))
	{
		scanStartOffset = SMS_STANDARD_TEXT_OFFSET;
	}

	if (scanStartOffset < textDecodeLength)
	{
		scanPayload = &payload[scanStartOffset];
		scanLength = (uint16_t)(textDecodeLength - scanStartOffset);
	}
	else
	{
		scanPayload = payload;
		scanLength = textDecodeLength;
	}

	decoded = smsDecodeMotorolaPayload(payload, totalLength, effectivePadOctets, decodedText);

	if (!decoded)
	{
		decoded = smsDecodeStandardPayload(payload, totalLength, effectivePadOctets, decodedText);
	}

	// Stage 2: direct decode in likely text windows (no brute-force raw replay).
	if ((decodedText[0] == 0) || (smsCountCharOccurrence(decodedText, '?') > 0U))
	{
		if ((hasMotorolaSignature || hasMotorolaPort) && (motorolaTextOffsetLength > 0U))
		{
			smsTryDecodeWindowDirect(&payload[SMS_MOTOROLA_TEXT_OFFSET], motorolaTextOffsetLength, true, decodedText, scratchText);
			smsTryDecodeWindowNeighborhood(payload,
				textDecodeLength,
				SMS_MOTOROLA_TEXT_OFFSET,
				SMS_WINDOW_NEIGHBORHOOD_RADIUS,
				SMS_WINDOW_NEIGHBORHOOD_STEP,
				true,
				decodedText,
				scratchText);
		}

		if ((hasStandardPort || hasMotorolaPort) && (standardTextOffsetLength > 0U))
		{
			bool allowStandardWindow = true;

			// For Motorola-style packets we prefer offset 38 and only try offset 32 as fallback
			// when nothing useful was decoded yet.
			if ((hasMotorolaSignature || hasMotorolaPort) && (decodedText[0] != 0))
			{
				allowStandardWindow = false;
			}

			if (allowStandardWindow)
			{
				smsTryDecodeWindowDirect(&payload[SMS_STANDARD_TEXT_OFFSET], standardTextOffsetLength, true, decodedText, scratchText);
				smsTryDecodeWindowNeighborhood(payload,
					textDecodeLength,
					SMS_STANDARD_TEXT_OFFSET,
					SMS_WINDOW_NEIGHBORHOOD_RADIUS,
					SMS_WINDOW_NEIGHBORHOOD_STEP,
					true,
					decodedText,
					scratchText);
			}
		}

		if ((hasMotorolaPort || hasStandardPort) && (udpPayloadLength > 0U))
		{
			smsTryDecodeWindowDirect(&payload[udpPayloadOffset], udpPayloadLength, false, decodedText, scratchText);
			smsTryDecodeWindowNeighborhood(payload,
				textDecodeLength,
				udpPayloadOffset,
				8U,
				2U,
				false,
				decodedText,
				scratchText);
		}
	}

	// Stage 3: expensive full-payload scans only when quality is still poor.
	if (((decodedText[0] == 0) || (smsCountCharOccurrence(decodedText, '?') > 0U)) &&
		smsDecodeUtf16LeRun(scanPayload, scanLength, scratchText, &utf16RunOffset) &&
		smsTryAdoptDecodedCandidate(decodedText, scratchText))
	{
		// Candidate accepted by quality score.
	}

	if (((decodedText[0] == 0) || (smsCountCharOccurrence(decodedText, '?') > 0U)) &&
		smsDecodeUtf16BeRun(scanPayload, scanLength, scratchText, &utf16BeRunOffset) &&
		smsTryAdoptDecodedCandidate(decodedText, scratchText))
	{
		// Candidate accepted by quality score.
	}

	if (((decodedText[0] == 0) || (smsCountCharOccurrence(decodedText, '?') > 0U)) &&
		smsDecodeUtf16PayloadByScan(scanPayload, scanLength, scratchText, &utf16ScanOffset) &&
		smsTryAdoptDecodedCandidate(decodedText, scratchText))
	{
		// Candidate accepted by quality score.
	}

	if (((decodedText[0] == 0) || (smsCountCharOccurrence(decodedText, '?') > 0U)) &&
		smsDecodeUtf8PayloadByScan(scanPayload, scanLength, scratchText, &utf8ScanOffset) &&
		smsTryAdoptDecodedCandidate(decodedText, scratchText))
	{
		// Candidate accepted by quality score.
	}

	decoded = (decodedText[0] != 0);

	if (decoded)
	{
		smsStoreInboxMessage(sourceId, decodedText);
	}

	return decoded;
}

static bool smsShouldAckUndecodedPayload(const uint8_t *payload, uint16_t totalLength, uint8_t padOctets)
{
	uint8_t effectivePadOctets = padOctets;
	uint16_t payloadLength;
	uint16_t textDecodeLength;
	uint16_t ipPacketLength = 0U;

	if ((payload == NULL) || (totalLength == 0U) || (effectivePadOctets >= totalLength))
	{
		return false;
	}

	if (effectivePadOctets >= SMS_BLOCK_DATA_BYTES)
	{
		effectivePadOctets = 0U;
	}

	payloadLength = (uint16_t)(totalLength - effectivePadOctets);
	textDecodeLength = payloadLength;

	if (payloadLength >= 4U)
	{
		ipPacketLength = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
		if ((ipPacketLength >= 20U) && (ipPacketLength <= payloadLength))
		{
			textDecodeLength = ipPacketLength;
		}
	}

	if (textDecodeLength >= 24U)
	{
		uint16_t srcPort = (uint16_t)(((uint16_t)payload[20] << 8) | payload[21]);
		uint16_t dstPort = (uint16_t)(((uint16_t)payload[22] << 8) | payload[23]);

		if ((srcPort == SMS_MOTOROLA_UDP_PORT) || (dstPort == SMS_MOTOROLA_UDP_PORT) ||
			(srcPort == SMS_STANDARD_UDP_PORT) || (dstPort == SMS_STANDARD_UDP_PORT))
		{
			return true;
		}
	}

	if (textDecodeLength >= 38U)
	{
		if ((payload[28] == 0x00U) &&
			((payload[30] == 0xA0U) || (payload[30] == 0xE0U)) &&
			(payload[31] == 0x00U))
		{
			return true;
		}
	}

	return false;
}

static void smsProcessPendingRxDecode(void)
{
	bool decoded = false;

	if (!rxDecodePending.active)
	{
		return;
	}

	if (HRC6000IRQHandlerIsRunning() || rxAssembly.active)
	{
		return;
	}

	if ((rxDecodePending.totalLength == 0U) || (rxDecodePending.totalLength > sizeof(smsRxDecodePendingPayloadBuffer)))
	{
		rxDecodePending.active = false;
		return;
	}

	decoded = smsDecodeCurrentRxBuffers(smsRxDecodePendingPayloadBuffer,
		rxDecodePending.totalLength,
		rxDecodePending.padOctets,
		rxDecodePending.sourceId);

	if (rxDecodePending.responseRequested && (rxDecodePending.sourceId != 0U) &&
		(decoded || smsShouldAckUndecodedPayload(smsRxDecodePendingPayloadBuffer,
			rxDecodePending.totalLength,
			rxDecodePending.padOctets)))
	{
		smsScheduleAckResponse(rxDecodePending.sourceId, rxDecodePending.ackProfile);
	}

	rxDecodePending.active = false;
}

void smsTick(void)
{
	smsProcessPendingRxDecode();

	if (ackResponseTracking.active &&
		(trxDMRID != 0U) &&
		(slotState == DMR_STATE_IDLE) &&
		!smsHasQueuedMessage() &&
		!HRC6000IsSendingSMS() &&
		!HRC6000IRQHandlerIsRunning())
	{
		if (smsQueueAckResponseMessage(ackResponseTracking.destinationId, trxDMRID, ackResponseTracking.ackProfile))
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
		// Some systems request response via bit6, others by dataPacketFormat 0x02/0x03.
		bool responseRequestedExplicit = (((frame[0] & 0x40U) != 0U) || (dataPacketFormat == 0x02U) || (dataPacketFormat == 0x03U));
		bool responseRequested = responseRequestedExplicit;
		uint8_t ackProfile = SMS_ACK_PROFILE_MODERN;
		uint8_t sapType;
		uint8_t blocks;
		uint8_t pad;
		uint32_t sourceId;

		if (smsHandleIncomingResponsePdu(frame))
		{
			return true;
		}

		sapType = (uint8_t)(frame[1] & 0xF0U);
		blocks = (uint8_t)(frame[8] & 0x7FU);
		pad = (uint8_t)((frame[0] & 0x10U) | (frame[1] & 0x0FU));
		sourceId = (((uint32_t)frame[5] << 16) | ((uint32_t)frame[6] << 8) | frame[7]);

		// Legacy/original firmwares can send valid inbound SMS headers as format 0x01
		// without setting the explicit response-request markers.
		if (!responseRequested && (dataPacketFormat == 0x01U))
		{
			responseRequested = true;

			// Keep hotspot/repeater compatibility as default. Only use legacy ACK profile
			// in same-ID simplex interoperability cases.
			if (sourceId == trxDMRID)
			{
				ackProfile = SMS_ACK_PROFILE_LEGACY;
			}
		}

		// Some repeaters may use dataPacketFormat 0x01 for inbound SMS headers.
		// Keep parsing instead of dropping those frames immediately.
		if ((dataPacketFormat == 0x03U) && (pad >= SMS_BLOCK_DATA_BYTES))
		{
			pad = 0U;
		}

		if ((sapType != 0x40U) || (blocks == 0U) || (blocks > SMS_MAX_RX_DATA_BLOCKS) || (pad >= SMS_BLOCK_DATA_BYTES))
		{
			smsResetRxAssembly();
			return false;
		}

		if (settingsIsOptionBitSet(BIT_SMS_FILTER_INCOMING_PC))
		{
			uint32_t destId = (((uint32_t)frame[2] << 16) | ((uint32_t)frame[3] << 8) | frame[4]);
			if (destId != trxDMRID)
			{
				smsResetRxAssembly();
				return false;
			}
		}

		rxAssembly.active = true;
		rxAssembly.expectedBlocks = blocks;
		rxAssembly.receivedBlocks = 0U;
		rxAssembly.payloadBytesReceived = 0U;
		rxAssembly.padOctets = pad;
		rxAssembly.responseRequested = responseRequested;
		rxAssembly.ackProfile = ackProfile;
		rxAssembly.sourceId = sourceId;
		memset(smsRxPayloadBuffer, 0, sizeof(smsRxPayloadBuffer));
		return true;
	}

	if (((dataType == 0x07U) || (dataType == 0x08U)) && rxAssembly.active)
	{
		uint8_t blockHeaderBytes = ((dataType == 0x08U) ? 2U : 0U);
		uint8_t blockPayloadBytes = (uint8_t)(SMS_BLOCK_DATA_BYTES - blockHeaderBytes);
		const uint8_t *blockPayload = &frame[blockHeaderBytes];

		if (rxAssembly.receivedBlocks >= rxAssembly.expectedBlocks)
		{
			smsResetRxAssembly();
			return false;
		}

		if ((rxAssembly.payloadBytesReceived + blockPayloadBytes) > sizeof(smsRxPayloadBuffer))
		{
			smsResetRxAssembly();
			return false;
		}

		memcpy(&smsRxPayloadBuffer[rxAssembly.payloadBytesReceived], blockPayload, blockPayloadBytes);
		rxAssembly.payloadBytesReceived = (uint16_t)(rxAssembly.payloadBytesReceived + blockPayloadBytes);
		rxAssembly.receivedBlocks++;

		if (rxAssembly.receivedBlocks >= rxAssembly.expectedBlocks)
		{
			uint16_t totalLength = rxAssembly.payloadBytesReceived;

			if ((totalLength == 0U) || (totalLength > sizeof(smsRxPayloadBuffer)))
			{
				smsResetRxAssembly();
				return false;
			}

			if (!rxDecodePending.active)
			{
				memcpy(smsRxDecodePendingPayloadBuffer, smsRxPayloadBuffer, totalLength);

				rxDecodePending.totalLength = totalLength;
				rxDecodePending.padOctets = rxAssembly.padOctets;
				rxDecodePending.responseRequested = rxAssembly.responseRequested;
				rxDecodePending.ackProfile = rxAssembly.ackProfile;
				rxDecodePending.sourceId = rxAssembly.sourceId;
				rxDecodePending.active = true;
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
	smsStorageHeader_t header;
	uint32_t messageAddress;

	if (message == NULL)
	{
		return false;
	}

	if (!smsStorageReadHeader(&header) || (index >= header.inboxCount))
	{
		return false;
	}

	messageAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, inboxMessages) + ((uint32_t)index * sizeof(smsInboxMessage_t)));
	if (!EEPROM_Read((int32_t)messageAddress, (uint8_t *)message, (int)sizeof(*message)))
	{
		return false;
	}

	message->text[SMS_MAX_TEXT_LENGTH] = 0;
	return true;
}

bool smsDeleteInboxMessage(uint8_t index)
{
	smsStorageHeader_t header;
	smsInboxMessage_t shiftedMessage;
	smsInboxMessage_t emptyMessage;
	uint32_t baseAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, inboxMessages));

	if (!smsStorageReadHeader(&header) || (index >= header.inboxCount))
	{
		return false;
	}

	for (uint8_t i = index; i < (uint8_t)(header.inboxCount - 1U); i++)
	{
		uint32_t fromAddress = (uint32_t)(baseAddress + ((uint32_t)(i + 1U) * sizeof(smsInboxMessage_t)));
		uint32_t toAddress = (uint32_t)(baseAddress + ((uint32_t)i * sizeof(smsInboxMessage_t)));

		if (!EEPROM_Read((int32_t)fromAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)) ||
			!EEPROM_Write((int32_t)toAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)))
		{
			return false;
		}
	}

	memset(&emptyMessage, 0, sizeof(emptyMessage));
	if (!EEPROM_Write((int32_t)(baseAddress + ((uint32_t)(header.inboxCount - 1U) * sizeof(smsInboxMessage_t))), (uint8_t *)&emptyMessage, (int)sizeof(emptyMessage)))
	{
		return false;
	}

	header.inboxCount--;
	if (!smsStorageWriteHeader(&header) || !smsStorageUpdateChecksum())
	{
		return false;
	}

	inboxCount = (uint8_t)header.inboxCount;
	if (inboxCount == 0U)
	{
		inboxUnreadNotification = false;
	}

	return true;
}

void smsClearInbox(void)
{
	smsStorageHeader_t header;

	if (!smsStorageReadHeader(&header))
	{
		memset(&header, 0, sizeof(header));
		header.magic = SMS_STORAGE_MAGIC;
		header.version = SMS_STORAGE_VERSION;
		header.sentCount = sentCount;
		header.quickTextCount = quickTextCount;
	}

	header.inboxCount = 0U;
	if (smsStorageWriteHeader(&header) && smsStorageUpdateChecksum())
	{
		inboxCount = 0U;
		inboxUnreadNotification = false;
	}

	smsResetRxAssembly();
}

uint8_t smsGetSentCount(void)
{
	return sentCount;
}

bool smsGetSentMessage(uint8_t index, smsSentMessage_t *message)
{
	smsStorageHeader_t header;
	uint32_t messageAddress;

	if (message == NULL)
	{
		return false;
	}

	if (!smsStorageReadHeader(&header) || (index >= header.sentCount))
	{
		return false;
	}

	messageAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, sentMessages) + ((uint32_t)index * sizeof(smsSentMessage_t)));
	if (!EEPROM_Read((int32_t)messageAddress, (uint8_t *)message, (int)sizeof(*message)))
	{
		return false;
	}

	message->text[SMS_MAX_TEXT_LENGTH] = 0;
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
	smsStorageHeader_t header;
	smsSentMessage_t shiftedMessage;
	smsSentMessage_t emptyMessage;
	uint32_t baseAddress = (uint32_t)(SMS_STORAGE_ADDRESS + offsetof(smsStorage_t, sentMessages));

	if (!smsStorageReadHeader(&header) || (index >= header.sentCount))
	{
		return false;
	}

	for (uint8_t i = index; i < (uint8_t)(header.sentCount - 1U); i++)
	{
		uint32_t fromAddress = (uint32_t)(baseAddress + ((uint32_t)(i + 1U) * sizeof(smsSentMessage_t)));
		uint32_t toAddress = (uint32_t)(baseAddress + ((uint32_t)i * sizeof(smsSentMessage_t)));

		if (!EEPROM_Read((int32_t)fromAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)) ||
			!EEPROM_Write((int32_t)toAddress, (uint8_t *)&shiftedMessage, (int)sizeof(shiftedMessage)))
		{
			return false;
		}
	}

	memset(&emptyMessage, 0, sizeof(emptyMessage));
	if (!EEPROM_Write((int32_t)(baseAddress + ((uint32_t)(header.sentCount - 1U) * sizeof(smsSentMessage_t))), (uint8_t *)&emptyMessage, (int)sizeof(emptyMessage)))
	{
		return false;
	}

	header.sentCount--;
	if (!smsStorageWriteHeader(&header) || !smsStorageUpdateChecksum())
	{
		return false;
	}

	sentCount = (uint8_t)header.sentCount;

	return true;
}

void smsClearSent(void)
{
	smsStorageHeader_t header;

	if (!smsStorageReadHeader(&header))
	{
		memset(&header, 0, sizeof(header));
		header.magic = SMS_STORAGE_MAGIC;
		header.version = SMS_STORAGE_VERSION;
		header.inboxCount = inboxCount;
		header.quickTextCount = quickTextCount;
	}

	header.sentCount = 0U;
	if (smsStorageWriteHeader(&header) && smsStorageUpdateChecksum())
	{
		sentCount = 0U;
	}
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
	message->text[SMS_MAX_TEXT_LENGTH] = 0;
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
	strncpy(storage.quickTextMessages[storage.quickTextCount].text, text, SMS_MAX_TEXT_LENGTH);
	storage.quickTextMessages[storage.quickTextCount].text[SMS_MAX_TEXT_LENGTH] = 0;
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
	strncpy(storage.quickTextMessages[index].text, text, SMS_MAX_TEXT_LENGTH);
	storage.quickTextMessages[index].text[SMS_MAX_TEXT_LENGTH] = 0;
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

	// Keep flash writes away from active DMR traffic and deferred SMS decode processing.
	if ((slotState != DMR_STATE_IDLE) || HRC6000IRQHandlerIsRunning() || HRC6000IsSendingSMS() || rxAssembly.active || rxDecodePending.active)
	{
		return;
	}

	if (smsStoragePersist())
	{
		smsStorageDirty = false;
		smsStorageDirtySinceTick = 0U;
	}
}
