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

#include "functions/sms.h"

static smsPreparedMessage_t queuedMessage;
static bool queuedMessageValid = false;

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
	message->dataHeader[0] = 0x02U | (uint8_t)(message->padOctetCount & 0x10U);  // Unconfirmed UDT + pad[4]
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