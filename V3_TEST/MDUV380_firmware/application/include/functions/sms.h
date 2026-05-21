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

#ifndef _OPENGD77_SMS_H_
#define _OPENGD77_SMS_H_

#include <stdint.h>
#include <stdbool.h>

#define SMS_MAX_TEXT_LENGTH           231U
#define SMS_MAX_UTF16_PAYLOAD_BYTES  (SMS_MAX_TEXT_LENGTH * 2U)
#define SMS_BLOCK_DATA_BYTES          12U
#define SMS_MOTOROLA_HEADER_BYTES     38U
#define SMS_STANDARD_CRC32_BYTES       4U
#define SMS_MAX_TRANSPORT_BYTES      (((SMS_MOTOROLA_HEADER_BYTES + SMS_MAX_UTF16_PAYLOAD_BYTES + SMS_STANDARD_CRC32_BYTES + SMS_BLOCK_DATA_BYTES - 1U) / SMS_BLOCK_DATA_BYTES) * SMS_BLOCK_DATA_BYTES)
#define SMS_MAX_DATA_BLOCKS          ((SMS_MAX_TRANSPORT_BYTES + SMS_BLOCK_DATA_BYTES - 1U) / SMS_BLOCK_DATA_BYTES)
#define SMS_MAX_RX_DATA_BLOCKS        63U
#define SMS_PREAMBLE_CSBKS            8U
#define SMS_MAX_TX_FRAMES            (SMS_PREAMBLE_CSBKS + 1U + SMS_MAX_DATA_BLOCKS)
#define SMS_INBOX_MAX_MESSAGES         8U
#define SMS_SENT_MAX_MESSAGES          8U
#define SMS_QUICKTEXT_MAX_MESSAGES    10U
#define SMS_QUICKTEXT_MAX_TITLE_LENGTH 16U

typedef enum
{
	SMS_PACK_OK = 0,
	SMS_PACK_ERROR_EMPTY,
	SMS_PACK_ERROR_TOO_LONG,
	SMS_PACK_ERROR_INVALID_DEST,
	SMS_PACK_ERROR_INVALID_SRC,
	SMS_PACK_ERROR_UNSUPPORTED_CHAR,
	SMS_PACK_ERROR_INVALID_INDEX
} smsPackResult_t;

typedef enum
{
	SMS_TX_EVENT_NONE = 0,
	SMS_TX_EVENT_SENDING,
	SMS_TX_EVENT_RETRYING,
	SMS_TX_EVENT_ACK,
	SMS_TX_EVENT_TIMEOUT,
	SMS_TX_EVENT_REJECTED,
	SMS_TX_EVENT_NO_REPEATER,
	SMS_TX_EVENT_SENT
} smsTxEvent_t;

typedef struct
{
	uint32_t destinationId;
	uint32_t sourceId;
	uint16_t payloadLength;
	uint8_t padOctetCount;
	uint8_t blockCount;
	bool requestAck;
	uint8_t csbk[SMS_BLOCK_DATA_BYTES];
	uint8_t dataHeader[SMS_BLOCK_DATA_BYTES];
	uint8_t blocks[SMS_MAX_DATA_BLOCKS][SMS_BLOCK_DATA_BYTES];
} smsPreparedMessage_t;

typedef struct
{
	uint32_t sourceId;
	char text[SMS_MAX_TEXT_LENGTH + 1U];
} smsInboxMessage_t;

typedef struct
{
	uint32_t destinationId;
	char text[SMS_MAX_TEXT_LENGTH + 1U];
} smsSentMessage_t;

typedef struct
{
	char title[SMS_QUICKTEXT_MAX_TITLE_LENGTH + 1U];
	char text[SMS_MAX_TEXT_LENGTH + 1U];
} smsQuickTextMessage_t;

void smsInit(void);
smsPackResult_t smsPackMessage(uint32_t destinationId, uint32_t sourceId, const char *text, smsPreparedMessage_t *message);
smsPackResult_t smsQueueMessage(uint32_t destinationId, uint32_t sourceId, const char *text);
bool smsHasQueuedMessage(void);
const smsPreparedMessage_t *smsGetQueuedMessage(void);
void smsClearQueuedMessage(void);
bool smsHandleReceivedDataFrame(uint8_t dataType, const uint8_t *frame);
uint8_t smsGetInboxCount(void);
bool smsGetInboxMessage(uint8_t index, smsInboxMessage_t *message);
bool smsDeleteInboxMessage(uint8_t index);
void smsClearInbox(void);
uint8_t smsGetSentCount(void);
bool smsGetSentMessage(uint8_t index, smsSentMessage_t *message);
bool smsStoreSentMessage(uint32_t destinationId, const char *text);
bool smsDeleteSentMessage(uint8_t index);
void smsClearSent(void);
smsPackResult_t smsQueueSentMessage(uint8_t index, uint32_t sourceId);
uint8_t smsGetQuickTextCount(void);
bool smsGetQuickTextMessage(uint8_t index, smsQuickTextMessage_t *message);
bool smsStoreQuickTextMessage(const char *title, const char *text);
bool smsUpdateQuickTextMessage(uint8_t index, const char *title, const char *text);
bool smsDeleteQuickTextMessage(uint8_t index);
bool smsHasRxNotification(void);
bool smsConsumeRxNotification(void);
bool smsScheduleQueuedMessageTransmission(uint32_t destinationId, uint32_t sourceId, const char *text, bool waitForAck, bool storeSent);
void smsNotifyOutgoingAckReceived(void);
void smsNotifyOutgoingRejected(void);
void smsNotifyOutgoingNoRepeater(void);
void smsNotifyOutgoingSent(void);
bool smsRetryLastOutgoingMessage(void);
smsTxEvent_t smsConsumeTxEvent(void);
void smsTick(void);
void smsInboxStorageTick(void);

#endif
