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
#include "functions/ticks.h"
#include "functions/trx.h"
#include "hardware/HR-C6000.h"
#include "hardware/EEPROM.h"

#define SMS_STORAGE_ADDRESS                    0x0F0000
#define SMS_STORAGE_MAGIC                      0x534D5349U
#define SMS_STORAGE_VERSION                    2U
#define SMS_STORAGE_DEBOUNCE_MS                1500U
#define SMS_TX_ACK_TIMEOUT_MS                  3000U
#define SMS_TX_RETRY_BACKOFF_MS                 250U
#define SMS_TX_MAX_RETRIES                        2U

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t inboxCount;
	uint32_t sentCount;
	uint32_t checksum;
	smsInboxMessage_t inboxMessages[SMS_INBOX_MAX_MESSAGES];
	smsSentMessage_t sentMessages[SMS_SENT_MAX_MESSAGES];
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
	bool active;
	uint8_t expectedBlocks;
	uint8_t receivedBlocks;
	uint8_t padOctets;
	bool responseRequested;
	uint32_t sourceId;
	uint8_t payload[SMS_MAX_DATA_BLOCKS * SMS_BLOCK_DATA_BYTES];
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
	uint8_t retriesRemaining;
	ticksTimer_t ackTimer;
	char text[SMS_MAX_TEXT_LENGTH + 1U];
} smsOutgoingTracking_t;

static smsInboxMessage_t inboxMessages[SMS_INBOX_MAX_MESSAGES];
static uint8_t inboxStart = 0U;
static uint8_t inboxCount = 0U;
static smsSentMessage_t sentMessages[SMS_SENT_MAX_MESSAGES];
static uint8_t sentStart = 0U;
static uint8_t sentCount = 0U;
static bool inboxUnreadNotification = false;
static smsRxAssembly_t rxAssembly = { 0 };
static smsAckResponseTracking_t ackResponseTracking = { 0 };
static smsOutgoingTracking_t outgoingTracking = { 0 };
static smsTxEvent_t pendingTxEvent = SMS_TX_EVENT_NONE;
static volatile bool smsStorageDirty = false;
static uint32_t smsStorageDirtySinceTick = 0U;

static void smsResetRxAssembly(void);
static void smsScheduleAckResponse(uint32_t destinationId);
static bool smsQueueAckResponseMessage(uint32_t destinationId, uint32_t sourceId);
static uint16_t smsCrc16Ccitt(const uint8_t *data, uint8_t length);
static void smsResetOutgoingTracking(void);
static void smsSetPendingTxEvent(smsTxEvent_t event);
static bool smsRetryOutgoingMessage(void);
static uint32_t smsStorageChecksum(const smsStorage_t *storage);
static uint32_t smsLegacyInboxStorageChecksum(const smsLegacyInboxStorage_t *storage);
static void smsStorageMarkDirty(void);
static void smsStorageBuildSnapshot(smsStorage_t *storage);
static bool smsStoragePersist(void);
static void smsStorageLoad(void);

void smsInit(void)
{
	memset(&queuedMessage, 0, sizeof(queuedMessage));
	queuedMessageValid = false;
	inboxStart = 0U;
	inboxCount = 0U;
	sentStart = 0U;
	sentCount = 0U;
	inboxUnreadNotification = false;
	memset(inboxMessages, 0, sizeof(inboxMessages));
	memset(sentMessages, 0, sizeof(sentMessages));
	smsResetRxAssembly();
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
	if (storage == NULL)
	{
		return;
	}

	memset(storage, 0, sizeof(*storage));
	storage->magic = SMS_STORAGE_MAGIC;
	storage->version = SMS_STORAGE_VERSION;
	storage->inboxCount = inboxCount;
	storage->sentCount = sentCount;

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
		(storage.checksum == smsStorageChecksum(&storage)))
	{
		inboxStart = 0U;
		inboxCount = (uint8_t)storage.inboxCount;
		sentStart = 0U;
		sentCount = (uint8_t)storage.sentCount;
		inboxUnreadNotification = false;
		memset(inboxMessages, 0, sizeof(inboxMessages));
		memset(sentMessages, 0, sizeof(sentMessages));

		for (uint8_t i = 0U; i < inboxCount; i++)
		{
			inboxMessages[i] = storage.inboxMessages[i];
			inboxMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
		}

		for (uint8_t i = 0U; i < sentCount; i++)
		{
			sentMessages[i] = storage.sentMessages[i];
			sentMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
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
	inboxUnreadNotification = false;
	memset(inboxMessages, 0, sizeof(inboxMessages));
	memset(sentMessages, 0, sizeof(sentMessages));

	for (uint8_t i = 0U; i < inboxCount; i++)
	{
		inboxMessages[i] = legacyStorage.messages[i];
		inboxMessages[i].text[SMS_MAX_TEXT_LENGTH] = 0;
	}
}

static bool smsIsPrintableCharacter(uint8_t c)
{
	return (((c >= 0x20U) && (c <= 0x7EU)) || (c == '\r') || (c == '\n'));
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
	strncpy(inboxMessages[writeIndex].text, text, SMS_MAX_TEXT_LENGTH);
	inboxMessages[writeIndex].text[SMS_MAX_TEXT_LENGTH] = 0;
	inboxUnreadNotification = true;
	smsStorageMarkDirty();
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
	strncpy(sentMessages[writeIndex].text, text, SMS_MAX_TEXT_LENGTH);
	sentMessages[writeIndex].text[SMS_MAX_TEXT_LENGTH] = 0;
	smsStorageMarkDirty();
}

static bool smsDecodeUtf16Payload(const uint8_t *payload, uint16_t payloadLength, char *textOut)
{
	uint16_t inIndex = 0U;
	uint8_t outIndex = 0U;

	if ((payload == NULL) || (textOut == NULL) || ((payloadLength & 0x01U) != 0U))
	{
		return false;
	}

	while ((inIndex + 1U) < payloadLength)
	{
		uint8_t high = payload[inIndex++];
		uint8_t low = payload[inIndex++];
		uint8_t c;

		if ((high == 0x00U) && smsIsPrintableCharacter(low))
		{
			c = low;
		}
		else if ((low == 0x00U) && smsIsPrintableCharacter(high))
		{
			c = high;
		}
		else
		{
			c = '?';
		}

		if (outIndex >= SMS_MAX_TEXT_LENGTH)
		{
			break;
		}

		textOut[outIndex++] = (char)c;
	}

	textOut[outIndex] = 0;
	return (outIndex > 0U);
}

static void smsResetRxAssembly(void)
{
	memset(&rxAssembly, 0, sizeof(rxAssembly));
}

static void smsScheduleAckResponse(uint32_t destinationId)
{
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
	queuedMessage.dataHeader[2] = (uint8_t)((destinationId >> 16) & 0xFFU);
	queuedMessage.dataHeader[3] = (uint8_t)((destinationId >> 8) & 0xFFU);
	queuedMessage.dataHeader[4] = (uint8_t)(destinationId & 0xFFU);
	queuedMessage.dataHeader[5] = (uint8_t)((sourceId >> 16) & 0xFFU);
	queuedMessage.dataHeader[6] = (uint8_t)((sourceId >> 8) & 0xFFU);
	queuedMessage.dataHeader[7] = (uint8_t)(sourceId & 0xFFU);
	queuedMessage.dataHeader[8] = 0x00U;
	queuedMessage.dataHeader[9] = 0x00U;
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
	message->dataHeader[0] = (message->requestAck ? 0x42U : 0x02U) | (uint8_t)(message->padOctetCount & 0x10U);
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

static smsPackResult_t smsConvertTextToUtf16Be(const char *text, uint8_t *payload, uint16_t *payloadLength)
{
	uint16_t index = 0;

	while (*text != 0)
	{
		unsigned char character = (unsigned char)(*text++);

		if (index >= SMS_MAX_UTF16_PAYLOAD_BYTES)
		{
			return SMS_PACK_ERROR_TOO_LONG;
		}

		if ((character < 0x20U) || (character > 0x7EU))
		{
			if ((character != '\r') && (character != '\n'))
			{
				return SMS_PACK_ERROR_UNSUPPORTED_CHAR;
			}
		}

		payload[index++] = 0x00U;
		payload[index++] = character;
	}

	*payloadLength = index;
	return ((index == 0U) ? SMS_PACK_ERROR_EMPTY : SMS_PACK_OK);
}

smsPackResult_t smsPackMessage(uint32_t destinationId, uint32_t sourceId, const char *text, smsPreparedMessage_t *message)
{
	smsPackResult_t result;
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

	result = smsConvertTextToUtf16Be(text, message->payload, &payloadLength);
	if (result != SMS_PACK_OK)
	{
		return result;
	}

	message->payloadLength = payloadLength;
	blockCount = (uint8_t)((payloadLength + (SMS_BLOCK_DATA_BYTES - 1U)) / SMS_BLOCK_DATA_BYTES);
	message->blockCount = blockCount;
	message->padOctetCount = (uint8_t)((blockCount * SMS_BLOCK_DATA_BYTES) - payloadLength);

	offset = 0U;
	for (uint8_t block = 0U; block < blockCount; block++)
	{
		uint8_t bytesToCopy = SMS_BLOCK_DATA_BYTES;
		if ((payloadLength - offset) < bytesToCopy)
		{
			bytesToCopy = (uint8_t)(payloadLength - offset);
		}

		memcpy(message->blocks[block], &message->payload[offset], bytesToCopy);
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
	outgoingTracking.retriesRemaining = SMS_TX_MAX_RETRIES;
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

static bool smsRetryOutgoingMessage(void)
{
	smsPackResult_t result;

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

	if (outgoingTracking.retriesRemaining > 0U)
	{
		outgoingTracking.retriesRemaining--;
	}

	ticksTimerStart(&outgoingTracking.ackTimer, SMS_TX_ACK_TIMEOUT_MS);
	smsSetPendingTxEvent(SMS_TX_EVENT_RETRYING);
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
	if (ackResponseTracking.active && (trxDMRID != 0U) && !smsHasQueuedMessage() && !HRC6000IsSendingSMS() && !HRC6000IRQHandlerIsRunning())
	{
		if (smsQueueAckResponseMessage(ackResponseTracking.destinationId, trxDMRID) && HRC6000StartQueuedSMS())
		{
			ackResponseTracking.active = false;
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

	if (outgoingTracking.retriesRemaining > 0U)
	{
		if (smsRetryOutgoingMessage())
		{
			return;
		}

		ticksTimerStart(&outgoingTracking.ackTimer, SMS_TX_RETRY_BACKOFF_MS);
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
		bool responseRequested = (((frame[0] & 0x40U) != 0U) || (dataPacketFormat == 0x03U));

		if (smsHandleIncomingResponsePdu(frame))
		{
			return true;
		}

		uint8_t sapType = (uint8_t)(frame[1] & 0xF0U);
		uint8_t blocks = (uint8_t)(frame[8] & 0x7FU);
		uint8_t pad = (uint8_t)((frame[0] & 0x10U) | (frame[1] & 0x0FU));

		if ((sapType != 0x40U) || (blocks == 0U) || (blocks > SMS_MAX_DATA_BLOCKS) || (pad >= SMS_BLOCK_DATA_BYTES))
		{
			smsResetRxAssembly();
			return false;
		}

		rxAssembly.active = true;
		rxAssembly.expectedBlocks = blocks;
		rxAssembly.receivedBlocks = 0U;
		rxAssembly.padOctets = pad;
		rxAssembly.responseRequested = responseRequested;
		rxAssembly.sourceId = (((uint32_t)frame[5] << 16) | ((uint32_t)frame[6] << 8) | frame[7]);
		memset(rxAssembly.payload, 0, sizeof(rxAssembly.payload));
		return true;
	}

	if ((dataType == 0x07U) && rxAssembly.active)
	{
		if (rxAssembly.receivedBlocks >= rxAssembly.expectedBlocks)
		{
			smsResetRxAssembly();
			return false;
		}

		memcpy(&rxAssembly.payload[rxAssembly.receivedBlocks * SMS_BLOCK_DATA_BYTES], frame, SMS_BLOCK_DATA_BYTES);
		rxAssembly.receivedBlocks++;

		if (rxAssembly.receivedBlocks >= rxAssembly.expectedBlocks)
		{
			uint16_t totalLength = (uint16_t)(rxAssembly.expectedBlocks * SMS_BLOCK_DATA_BYTES);
			uint16_t payloadLength = (uint16_t)(totalLength - rxAssembly.padOctets);
			char decodedText[SMS_MAX_TEXT_LENGTH + 1U] = { 0 };

			if ((payloadLength > 0U) && (payloadLength <= sizeof(rxAssembly.payload)) &&
				smsDecodeUtf16Payload(rxAssembly.payload, payloadLength, decodedText))
			{
				smsStoreInboxMessage(rxAssembly.sourceId, decodedText);

				if (rxAssembly.responseRequested && (rxAssembly.sourceId != trxDMRID))
				{
					smsScheduleAckResponse(rxAssembly.sourceId);
				}
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
